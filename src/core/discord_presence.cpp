#include "discord_presence.hpp"

#include <cstdlib>
#include <string_view>
#include <tuple>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace core {
namespace discord = utils::discord;

// One connection to the Discord client. Discord listens on discord-ipc-0, or
// the next free number (up to 9) when several clients run at once.
class DiscordPresence::Pipe {
public:
    Pipe() = default;
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    ~Pipe() { close(); }

#ifdef _WIN32
    bool open()
    {
        close();
        for (int i{ 0 }; i < 10; ++i) {
            const std::wstring path{ L"\\\\?\\pipe\\discord-ipc-" + std::to_wstring(i) };
            handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) {
                return true;
            }
        }
        return false;
    }

    void close()
    {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    [[nodiscard]] bool is_open() const { return handle_ != INVALID_HANDLE_VALUE; }

    bool write(const std::vector<std::byte>& data)
    {
        DWORD written{ 0 };
        return is_open()
            && WriteFile(handle_, data.data(), static_cast<DWORD>(data.size()), &written, nullptr)
            && written == data.size();
    }

    // Appends whatever has arrived without waiting. False when the pipe broke.
    bool read_available(std::vector<std::byte>& buffer)
    {
        DWORD available{ 0 };
        if (!is_open() || !PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) {
            return false;
        }

        while (available > 0) {
            std::byte chunk[4096];
            DWORD read{ 0 };
            const DWORD wanted{ available < sizeof(chunk) ? available : static_cast<DWORD>(sizeof(chunk)) };
            if (!ReadFile(handle_, chunk, wanted, &read, nullptr) || read == 0) {
                return false;
            }
            buffer.insert(buffer.end(), chunk, chunk + read);
            available -= read;
        }
        return true;
    }

private:
    HANDLE handle_{ INVALID_HANDLE_VALUE };
#else
    bool open()
    {
        close();
        std::string_view dir{ "/tmp" };
        for (const char* name : { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" }) {
            if (const char* value{ std::getenv(name) }; value && *value) {
                dir = value;
                break;
            }
        }

        for (int i{ 0 }; i < 10; ++i) {
            const auto path{ fmt::format("{}/discord-ipc-{}", dir, i) };
            sockaddr_un address{};
            if (path.size() >= sizeof(address.sun_path)) {
                return false;
            }
            address.sun_family = AF_UNIX;
            path.copy(address.sun_path, path.size());

            socket_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (socket_ < 0) {
                return false;
            }
            if (::connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
                return true;
            }
            close();
        }
        return false;
    }

    void close()
    {
        if (socket_ >= 0) {
            ::close(socket_);
            socket_ = -1;
        }
    }

    [[nodiscard]] bool is_open() const { return socket_ >= 0; }

    bool write(const std::vector<std::byte>& data)
    {
        std::size_t sent{ 0 };
        while (is_open() && sent < data.size()) {
            const auto n{ ::send(socket_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL) };
            if (n <= 0) {
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return sent == data.size();
    }

    bool read_available(std::vector<std::byte>& buffer)
    {
        if (!is_open()) {
            return false;
        }

        for (;;) {
            std::byte chunk[4096];
            const auto n{ ::recv(socket_, chunk, sizeof(chunk), MSG_DONTWAIT) };
            if (n > 0) {
                buffer.insert(buffer.end(), chunk, chunk + n);
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return true;
            }
            return false; // 0: closed by Discord, or a real error
        }
    }

private:
    int socket_{ -1 };
#endif
};

DiscordPresence::DiscordPresence(std::string application_id, discord::Activity activity)
    : application_id_{ std::move(application_id) }
    , activity_{ std::move(activity) }
{
    if (application_id_.empty()) {
        spdlog::info("Discord Rich Presence is off: no Discord application id set");
        return;
    }

    worker_ = std::jthread{ [this](const std::stop_token stop) { run(stop); } };
}

DiscordPresence::~DiscordPresence()
{
    if (worker_.joinable()) {
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
    }
}

void DiscordPresence::set_details(std::string details)
{
    {
        std::scoped_lock lock{ mutex_ };
        if (activity_.details == details) {
            return;
        }
        activity_.details = std::move(details);
        dirty_ = true;
    }
    wake_.notify_all();
}

void DiscordPresence::run(const std::stop_token stop)
{
#ifdef _WIN32
    const auto pid{ static_cast<std::uint32_t>(GetCurrentProcessId()) };
#else
    const auto pid{ static_cast<std::uint32_t>(::getpid()) };
#endif

    Pipe pipe{};
    std::vector<std::byte> buffer{};
    auto next_attempt{ std::chrono::steady_clock::now() };

    while (!stop.stop_requested()) {
        const auto now{ std::chrono::steady_clock::now() };

        if (!pipe.is_open() && now >= next_attempt) {
            next_attempt = now + RETRY_INTERVAL;
            buffer.clear();
            ready_ = false;
            if (pipe.open()) {
                if (!pipe.write(discord::make_frame(discord::Opcode::Handshake, discord::handshake_json(application_id_)))) {
                    pipe.close();
                }
            }
            else if (!logged_unavailable_) {
                logged_unavailable_ = true;
                spdlog::info("Discord Rich Presence: Discord is not running, will keep trying");
            }
        }

        if (pipe.is_open()) {
            if (!pipe.read_available(buffer) || !handle_frames(pipe, buffer)) {
                pipe.close();
                ready_ = false;
                spdlog::info("Discord Rich Presence: disconnected from Discord");
            }
            else if (ready_) {
                std::string payload{};
                {
                    std::scoped_lock lock{ mutex_ };
                    if (dirty_) {
                        payload = discord::set_activity_json(pid, activity_, ++nonce_);
                        dirty_ = false;
                    }
                }
                if (!payload.empty() && !pipe.write(discord::make_frame(discord::Opcode::Frame, payload))) {
                    pipe.close();
                    ready_ = false;
                    std::scoped_lock lock{ mutex_ };
                    dirty_ = true;
                }
            }
        }

        std::unique_lock lock{ mutex_ };
        wake_.wait_for(lock, stop, POLL_INTERVAL, [this] { return dirty_ && ready_; });
    }
}

bool DiscordPresence::handle_frames(Pipe& pipe, std::vector<std::byte>& buffer)
{
    while (const auto header{ discord::read_header(buffer.data(), buffer.size()) }) {
        if (buffer.size() < discord::HEADER_SIZE + header->length) {
            return true; // Wait for the rest of the frame.
        }

        const std::string_view json{
            reinterpret_cast<const char*>(buffer.data() + discord::HEADER_SIZE),
            header->length
        };

        switch (header->opcode) {
        case discord::Opcode::Frame:
            if (discord::has_string_field(json, "evt", "READY")) {
                ready_ = true;
                logged_unavailable_ = false;
                {
                    std::scoped_lock lock{ mutex_ };
                    dirty_ = true; // (Re)send the activity on every new connection.
                }
                spdlog::info("Discord Rich Presence connected");
            }
            else if (discord::has_string_field(json, "evt", "ERROR")) {
                spdlog::warn("Discord Rich Presence error: {}", json);
            }
            break;
        case discord::Opcode::Ping:
            std::ignore = pipe.write(discord::make_frame(discord::Opcode::Pong, json));
            break;
        case discord::Opcode::Close:
            // Sent for a wrong application id among others; say why, once.
            if (json != last_close_reason_) {
                last_close_reason_ = json;
                spdlog::warn("Discord closed the Rich Presence connection: {}", json);
            }
            buffer.clear();
            return false;
        default:
            break;
        }

        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(discord::HEADER_SIZE + header->length));
    }

    // Garbage that does not parse as a header: start over.
    return buffer.size() < discord::HEADER_SIZE;
}
}
