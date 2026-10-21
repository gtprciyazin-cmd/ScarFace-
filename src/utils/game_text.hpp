#pragma once
#include <charconv>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

#include "color_codes.hpp"

// Reading the server's own texts (console lines, dialogs).
namespace utils::game_text {
// World chat comes as "CP:0_PL:4_OID:_CT:[W]_ <Name> ..."; anything a player
// can type must never be taken for a server notice.
[[nodiscard]] inline bool is_player_chat(const std::string_view console_text)
{
    return console_text.starts_with("CP:") || console_text.find("_CT:[") != std::string_view::npos;
}

struct AccessOffer {
    std::string owner;
    std::string lock;
};

// "X wants to add you to a World Lock. Wrench yourself to accept."
[[nodiscard]] inline std::optional<AccessOffer> parse_access_offer(const std::string_view console_text)
{
    if (is_player_chat(console_text)) {
        return std::nullopt;
    }

    static const std::regex offer{ R"(^\s*(.+?) wants to add you to an? (.+?)\. Wrench yourself to accept\.)" };
    const auto text{ strip_color_codes(console_text) };
    std::smatch match{};
    if (!std::regex_search(text, match, offer)) {
        return std::nullopt;
    }

    return AccessOffer{ match[1].str(), match[2].str() };
}

// The trash dialog's "How many to `4destroy``? (you have 37)" -> 37; 0 when absent.
[[nodiscard]] inline int trash_owned_amount(const std::string_view dialog)
{
    constexpr std::string_view marker{ "you have " };
    const auto at{ dialog.find(marker) };
    if (at == std::string_view::npos) {
        return 0;
    }

    int value{ 0 };
    const auto* begin{ dialog.data() + at + marker.size() };
    const auto [ptr, ec]{ std::from_chars(begin, dialog.data() + dialog.size(), value) };
    return ec == std::errc{} && ptr != begin ? value : 0;
}
}
