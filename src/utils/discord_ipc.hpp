#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

// Discord's local RPC protocol, as spoken by the desktop client over a named
// pipe (Windows: \\?\pipe\discord-ipc-N) or a Unix socket (discord-ipc-N).
// Every message is a frame: opcode (u32 LE), payload length (u32 LE), then
// that many bytes of UTF-8 JSON.
namespace utils::discord {
enum class Opcode : std::uint32_t {
    Handshake = 0,
    Frame = 1,
    Close = 2,
    Ping = 3,
    Pong = 4,
};

struct FrameHeader {
    Opcode opcode;
    std::uint32_t length;
};

inline constexpr std::size_t HEADER_SIZE{ 8 };
// Discord rejects anything larger; activities are far below this.
inline constexpr std::uint32_t MAX_FRAME_SIZE{ 64 * 1024 };

struct Button {
    std::string label; // at most 32 characters
    std::string url;
};

struct Activity {
    std::string details;
    std::string state;
    std::int64_t start_timestamp{ 0 }; // Unix seconds; 0 hides the timer
    std::string large_image;
    std::string large_text;
    std::string small_image;
    std::string small_text;
    std::vector<Button> buttons; // at most 2
};

[[nodiscard]] inline std::string json_escape(const std::string_view text)
{
    std::string out{};
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += fmt::format("\\u{:04x}", static_cast<unsigned char>(c));
            }
            else {
                out += c;
            }
        }
    }
    return out;
}

[[nodiscard]] inline std::vector<std::byte> make_frame(const Opcode opcode, const std::string_view json)
{
    std::vector<std::byte> frame(HEADER_SIZE + json.size());
    const auto op{ static_cast<std::uint32_t>(opcode) };
    const auto length{ static_cast<std::uint32_t>(json.size()) };
    for (std::size_t i{ 0 }; i < 4; ++i) {
        frame[i] = static_cast<std::byte>((op >> (8 * i)) & 0xff);
        frame[4 + i] = static_cast<std::byte>((length >> (8 * i)) & 0xff);
    }
    std::memcpy(frame.data() + HEADER_SIZE, json.data(), json.size());
    return frame;
}

[[nodiscard]] inline std::optional<FrameHeader> read_header(const std::byte* data, const std::size_t size)
{
    if (size < HEADER_SIZE) {
        return std::nullopt;
    }

    std::uint32_t op{ 0 };
    std::uint32_t length{ 0 };
    for (std::size_t i{ 0 }; i < 4; ++i) {
        op |= static_cast<std::uint32_t>(data[i]) << (8 * i);
        length |= static_cast<std::uint32_t>(data[4 + i]) << (8 * i);
    }

    if (op > static_cast<std::uint32_t>(Opcode::Pong) || length > MAX_FRAME_SIZE) {
        return std::nullopt;
    }

    return FrameHeader{ static_cast<Opcode>(op), length };
}

// True when the JSON object has "key": "value" at any level, with any
// spacing around the colon. Enough to spot the READY and ERROR events.
[[nodiscard]] inline bool has_string_field(const std::string_view json, const std::string_view key, const std::string_view value)
{
    const auto quoted_key{ fmt::format("\"{}\"", key) };
    for (auto pos{ json.find(quoted_key) }; pos != std::string_view::npos; pos = json.find(quoted_key, pos + 1)) {
        auto i{ pos + quoted_key.size() };
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) {
            ++i;
        }
        if (i >= json.size() || json[i] != ':') {
            continue;
        }
        ++i;
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) {
            ++i;
        }
        if (json.substr(i).starts_with(fmt::format("\"{}\"", value))) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline std::string handshake_json(const std::string_view client_id)
{
    return fmt::format(R"({{"v":1,"client_id":"{}"}})", json_escape(client_id));
}

// SET_ACTIVITY for process `pid`. Empty fields are left out, since Discord
// rejects empty strings in most of them.
[[nodiscard]] inline std::string set_activity_json(const std::uint32_t pid, const Activity& activity, const std::uint64_t nonce)
{
    std::string fields{};
    const auto add{ [&fields](const std::string_view key, const std::string& value) {
        if (value.empty()) {
            return;
        }
        if (!fields.empty()) {
            fields += ',';
        }
        fields += fmt::format(R"("{}":"{}")", key, json_escape(value));
    } };

    add("details", activity.details);
    add("state", activity.state);

    if (activity.start_timestamp > 0) {
        fields += fmt::format(R"({}"timestamps":{{"start":{}}})", fields.empty() ? "" : ",", activity.start_timestamp);
    }

    std::string assets{};
    const auto add_asset{ [&assets](const std::string_view key, const std::string& value) {
        if (value.empty()) {
            return;
        }
        if (!assets.empty()) {
            assets += ',';
        }
        assets += fmt::format(R"("{}":"{}")", key, json_escape(value));
    } };
    add_asset("large_image", activity.large_image);
    add_asset("large_text", activity.large_text);
    add_asset("small_image", activity.small_image);
    add_asset("small_text", activity.small_text);
    if (!assets.empty()) {
        fields += fmt::format(R"({}"assets":{{{}}})", fields.empty() ? "" : ",", assets);
    }

    std::string buttons{};
    std::size_t button_count{ 0 };
    for (const auto& button : activity.buttons) {
        if (button.label.empty() || button.url.empty() || button_count == 2) {
            continue;
        }
        if (!buttons.empty()) {
            buttons += ',';
        }
        buttons += fmt::format(
            R"({{"label":"{}","url":"{}"}})",
            json_escape(button.label.substr(0, 32)),
            json_escape(button.url)
        );
        ++button_count;
    }
    if (!buttons.empty()) {
        fields += fmt::format(R"({}"buttons":[{}])", fields.empty() ? "" : ",", buttons);
    }

    return fmt::format(
        R"({{"cmd":"SET_ACTIVITY","args":{{"pid":{},"activity":{{{}}}}},"nonce":"{}"}})",
        pid,
        fields,
        nonce
    );
}
}
