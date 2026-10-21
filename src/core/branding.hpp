#pragma once
#include <string_view>

namespace core::branding {
inline constexpr std::string_view PRODUCT_NAME{ "Scarface Proxy" };
inline constexpr std::string_view VERSION{ "v3.0" };
inline constexpr std::string_view TAGLINE{ "The World Is Yours" };
// Discord application shown as the activity ("Scarface Premium Proxy") and
// the invite behind its button; config.json can override both.
inline constexpr std::string_view DISCORD_APP_ID{ "1559991788142333982" };
inline constexpr std::string_view DISCORD_INVITE{ "https://discord.gg/mdpvp" };
// Put in front of every message the proxy shows in the game console.
inline constexpr std::string_view LOG_PREFIX{ "`9[Scarface]`` " };

// Shown in the console on startup, above the license-key prompt. Uses
// box-drawing block glyphs (UTF-8); main() sets the console output code page to
// UTF-8 and enables ANSI colour so it renders in Scarface red.
inline constexpr std::string_view LOGO{
R"( ███████╗ ██████╗ █████╗ ██████╗ ███████╗ █████╗  ██████╗███████╗
 ██╔════╝██╔════╝██╔══██╗██╔══██╗██╔════╝██╔══██╗██╔════╝██╔════╝
 ███████╗██║     ███████║██████╔╝█████╗  ███████║██║     █████╗
 ╚════██║██║     ██╔══██║██╔══██╗██╔══╝  ██╔══██║██║     ██╔══╝
 ███████║╚██████╗██║  ██║██║  ██║██║     ██║  ██║╚██████╗███████╗
 ╚══════╝ ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝ ╚═════╝╚══════╝)"
};
}
