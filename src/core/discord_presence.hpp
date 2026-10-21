#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../utils/discord_ipc.hpp"

namespace core {
// Shows the proxy as an activity on the user's Discord profile ("Scarface
// Premium Proxy", details, licensed role, elapsed time, buttons) through the
// Discord desktop client's local IPC pipe. Runs on its own thread, keeps
// retrying while Discord is closed and survives Discord restarts. Without an
// application id it does nothing.
class DiscordPresence {
public:
    DiscordPresence(std::string application_id, utils::discord::Activity activity);
    ~DiscordPresence();

    DiscordPresence(const DiscordPresence&) = delete;
    DiscordPresence& operator=(const DiscordPresence&) = delete;
    DiscordPresence(DiscordPresence&&) noexcept = delete;
    DiscordPresence& operator=(DiscordPresence&&) noexcept = delete;

    // Changes the first line under the title, e.g. "Currently In-game".
    // Safe to call from any thread.
    void set_details(std::string details);

private:
    class Pipe;

    static constexpr auto RETRY_INTERVAL{ std::chrono::seconds{ 15 } };
    static constexpr auto POLL_INTERVAL{ std::chrono::milliseconds{ 500 } };

    void run(std::stop_token stop);
    // Handles every complete frame in `buffer`. Returns false when Discord
    // closed the connection.
    bool handle_frames(Pipe& pipe, std::vector<std::byte>& buffer);

    const std::string application_id_;

    std::mutex mutex_;
    std::condition_variable_any wake_;
    utils::discord::Activity activity_;
    bool dirty_{ true };

    // Only touched by the worker thread.
    bool ready_{ false };
    std::uint64_t nonce_{ 0 };
    bool logged_unavailable_{ false };
    std::string last_close_reason_;

    std::jthread worker_;
};
}
