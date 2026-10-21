#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "color_codes.hpp"

namespace utils::roulette {
struct SpinResult {
    std::string spinner;    // Name of the player who spun, colour codes removed.
    int number{ 0 };        // 0-36
    std::string color_code; // Growtopia colour code of the number, e.g. "`4".
};

using utils::strip_color_codes;

// "Reme" as played in Growtopia casinos: the last digit of the sum of the
// number's digits (22 -> 4, 28 -> 10 -> 0).
[[nodiscard]] constexpr int reme(const int number)
{
    return (number / 10 + number % 10) % 10;
}

// Colour of a number on the wheel, used when the message carries none.
[[nodiscard]] inline std::string_view number_color_code(const int number)
{
    constexpr std::array RED{ 1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36 };
    if (number == 0) {
        return "`2";
    }

    return std::ranges::find(RED, number) != RED.end() ? "`4" : "`b";
}

// Chat lines carry a routing prefix the client strips before showing them:
// "CP:0_PL:0_OID:_CT:[W]_ " in the console, "CP:0_PL:0_OID:_player_chat=" in
// a bubble. Returns its length; 0 for the server's own lines.
[[nodiscard]] inline std::size_t chat_prefix_length(const std::string_view text)
{
    std::size_t pos{ 0 };
    const auto skip_field{ [&](const std::string_view tag) {
        if (text.substr(pos).starts_with(tag)) {
            const auto end{ text.find('_', pos + tag.size()) };
            pos = end == std::string_view::npos ? text.size() : end + 1;
        }
    } };

    skip_field("CP:");
    skip_field("PL:");
    skip_field("OID:");
    if (pos > 0 && pos < text.size() && text[pos] == '_') {
        ++pos;
    }
    if (text.substr(pos).starts_with("CT:[")) {
        const auto end{ text.find("]_", pos) };
        pos = end == std::string_view::npos ? text.size() : end + 2;
        while (pos < text.size() && text[pos] == ' ') {
            ++pos;
        }
    }
    if (text.substr(pos).starts_with("player_chat=")) {
        pos += std::string_view{ "player_chat=" }.size();
    }
    return pos;
}

// True for anything a player typed (world chat, bubbles, /me, PMs).
[[nodiscard]] inline bool is_player_text(const std::string_view text)
{
    return text.starts_with("CP:") || text.find("player_chat=") != std::string_view::npos || text.find("CT:[") != std::string_view::npos;
}

// Parses the server's roulette message, e.g.
// "`7[```wPlayer`` spun the wheel and got `422``!`7]``".
[[nodiscard]] inline std::optional<SpinResult> parse_spin_message(std::string_view text)
{
    constexpr std::string_view PHRASE{ " spun the wheel and got " };

    // Typed copies come behind a chat prefix; read the spin itself.
    text.remove_prefix(chat_prefix_length(text));
    const std::string plain{ strip_color_codes(text) };
    const auto phrase_pos{ plain.find(PHRASE) };
    if (phrase_pos == std::string::npos) {
        return std::nullopt;
    }

    std::size_t digits_begin{ phrase_pos + PHRASE.size() };
    std::size_t digits_end{ digits_begin };
    while (digits_end < plain.size() && std::isdigit(static_cast<unsigned char>(plain[digits_end]))) {
        ++digits_end;
    }

    if (digits_end == digits_begin || digits_end - digits_begin > 2) {
        return std::nullopt;
    }

    SpinResult result{};
    result.number = std::stoi(plain.substr(digits_begin, digits_end - digits_begin));
    if (result.number > 36) {
        return std::nullopt;
    }

    std::string_view spinner{ plain };
    spinner = spinner.substr(0, phrase_pos);
    while (!spinner.empty() && (spinner.front() == '[' || std::isspace(static_cast<unsigned char>(spinner.front())))) {
        spinner.remove_prefix(1);
    }
    while (!spinner.empty() && std::isspace(static_cast<unsigned char>(spinner.back()))) {
        spinner.remove_suffix(1);
    }
    result.spinner = spinner;

    // Keep the colour the server used for the number if it sent one.
    constexpr std::string_view GOT{ "and got " };
    if (const auto got_pos{ text.find(GOT) }; got_pos != std::string_view::npos) {
        const auto code_pos{ got_pos + GOT.size() };
        if (code_pos + 1 < text.size() && text[code_pos] == '`' && text[code_pos + 1] != '`') {
            result.color_code = text.substr(code_pos, 2);
        }
    }

    if (result.color_code.empty()) {
        result.color_code = number_color_code(result.number);
    }

    return result;
}
}

namespace utils::roulette {
// A spin is real only when the server itself announced it: its result is
// shown with a delay (when the wheel stops) and never behind a chat prefix.
// In a bubble the avatar it sits on must also be the player named in it.
[[nodiscard]] inline bool is_real_spin(const std::string_view text, const std::int32_t delay_ms, const std::optional<std::string_view> bubble_owner = std::nullopt)
{
    if (delay_ms <= 0 || is_player_text(text)) {
        return false;
    }

    const auto spin{ parse_spin_message(text) };
    if (!spin) {
        return false;
    }

    if (bubble_owner) {
        auto owner{ strip_color_codes(*bubble_owner) };
        // Titles around the name: "Dr. Name", "Name of Legend".
        if (owner.starts_with("Dr.")) {
            owner.erase(0, 3);
        }
        if (owner.ends_with(" of Legend")) {
            owner.erase(owner.size() - 10);
        }
        const auto trim{ [](std::string& s) {
            s.erase(0, s.find_first_not_of(' '));
            s.erase(s.find_last_not_of(' ') + 1);
        } };
        auto spinner{ spin->spinner };
        trim(owner);
        trim(spinner);
        if (spinner.starts_with("Dr.")) {
            spinner.erase(0, 3);
            trim(spinner);
        }
        if (owner != spinner) {
            return false;
        }
    }

    return true;
}

// Puts `tag` in front of what the line shows, behind any chat prefix (the
// client throws away everything up to the end of that prefix).
[[nodiscard]] inline std::string tag_message(const std::string_view text, const std::string_view tag)
{
    const auto prefix{ chat_prefix_length(text) };
    return fmt::format("{}{} {}", text.substr(0, prefix), tag, text.substr(prefix));
}
}
