#pragma once
#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <fmt/format.h>

#include "../command.hpp"
#include "../command_registry.hpp"
#include "../../core/handlers/zoom_handler.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../packet/message/chat.hpp"

namespace command {
// "/zoom" toggles the zoomed-out view, "/zoom <level>" sets it (smaller is
// further out), "/zoom off" goes back to the game's zoom.
class ZoomCommand final : public ICommand {
public:
    explicit ZoomCommand(core::handlers::ZoomHandler& zoom)
        : zoom_{ zoom }
    {

    }

    [[nodiscard]] std::string_view name() const override { return "zoom"; }
    [[nodiscard]] std::string description() const override { return "Zoom out to see the whole world"; }

    Result execute(const Context& ctx) override
    {
        using core::handlers::ZoomHandler;

        std::string arg{ ctx.args.empty() ? "" : ctx.args[0] };
        std::ranges::transform(arg, arg.begin(), [](const unsigned char c) { return std::tolower(c); });

        if (arg.empty() || arg == "on" || arg == "off") {
            zoom_.set_enabled(arg.empty() ? !zoom_.is_enabled() : arg == "on");
            send_log(ctx, zoom_.is_enabled()
                ? fmt::format("`2Zoom out ``({:g}). Scrolling the mouse wheel undoes it; type {}zoom again.", zoom_.get_level(), ctx.registry.prefix())
                : std::string{ "`4Zoom back to normal." });
            return Result::Success;
        }

        float level{ 0.0f };
        try {
            level = std::stof(arg);
        }
        catch (...) {
            send_log(ctx, fmt::format("`4Usage: ``{0}zoom``, ``{0}zoom <{1:g}-{2:g}>`` (smaller is further out), ``{0}zoom off``",
                ctx.registry.prefix(), ZoomHandler::MIN_LEVEL, ZoomHandler::MAX_LEVEL));
            return Result::InvalidArguments;
        }

        send_log(ctx, fmt::format("`2Zoom ``{:g}", zoom_.set_level(level)));
        return Result::Success;
    }

private:
    static void send_log(const Context& ctx, const std::string& msg)
    {
        packet::message::Log log{};
        log.msg = msg;
        std::ignore = packet::PacketHelper::write(log, ctx.server);
    }

    core::handlers::ZoomHandler& zoom_;
};
}
