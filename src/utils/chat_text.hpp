#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace utils {
// Makes `text` safe to send as one chat line: drops newlines, pipes and other
// control characters (they would end the line early and let the rest be read
// as another field of the packet), trims spaces and cuts it to `max_length`.
[[nodiscard]] inline std::string sanitize_chat_message(const std::string_view text, const std::size_t max_length)
{
    std::string clean{};
    clean.reserve(std::min(text.size(), max_length));
    for (const char c : text) {
        if (c == '|' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
            continue;
        }

        clean += c;
    }

    const auto first{ clean.find_first_not_of(' ') };
    if (first == std::string::npos) {
        return {};
    }
    clean.erase(0, first);
    clean.erase(clean.find_last_not_of(' ') + 1);

    if (clean.size() > max_length) {
        clean.resize(max_length);
        // Do not cut a multi-byte UTF-8 character in half.
        while (!clean.empty() && (static_cast<unsigned char>(clean.back()) & 0xc0) == 0x80) {
            clean.pop_back();
        }
        if (!clean.empty() && static_cast<unsigned char>(clean.back()) >= 0xc0) {
            clean.pop_back();
        }
    }

    // A lone backtick at the end would swallow the next character on screen.
    while (!clean.empty() && clean.back() == '`' && (clean.size() < 2 || clean[clean.size() - 2] != '`')) {
        clean.pop_back();
        while (!clean.empty() && clean.back() == ' ') {
            clean.pop_back();
        }
    }

    // Only colour codes: the server ignores it.
    bool visible{ false };
    for (std::size_t i{ 0 }; i < clean.size() && !visible; ++i) {
        if (clean[i] == '`') {
            ++i;
        }
        else if (clean[i] != ' ') {
            visible = true;
        }
    }

    return visible ? clean : std::string{};
}

// Growtopia colour code for a ping: green below 100 ms, yellow below 200 ms,
// red above.
[[nodiscard]] inline char ping_colour(const std::uint32_t ping_ms)
{
    return ping_ms < 100 ? '2' : ping_ms < 200 ? '9' : '4';
}

// "45 ms" in the colour for that ping.
[[nodiscard]] inline std::string format_ping(const std::uint32_t ping_ms)
{
    return fmt::format("`{}{} ms``", ping_colour(ping_ms), ping_ms);
}

// The player's name with the ping after it. `` first so a colour the name
// leaves open does not tint the ping.
[[nodiscard]] inline std::string name_with_ping(const std::string_view name, const std::uint32_t ping_ms)
{
    const auto ping{ fmt::format("`` `{}({} ms)``", ping_colour(ping_ms), ping_ms) };

    // The client only shows the legend flag while the name ends with this, so
    // the ping goes in front of it, with the name's colour picked up again.
    constexpr std::string_view legend{ " of Legend``" };
    if (name.size() > legend.size() && name.ends_with(legend)) {
        const std::string_view colour{ name.size() >= 2 && name[0] == '`' ? name.substr(0, 2) : std::string_view{} };
        return fmt::format("{}{}{}{}", name.substr(0, name.size() - legend.size()), ping, colour, legend);
    }

    return fmt::format("{}{}", name, ping);
}
}
