#pragma once
#include <charconv>
#include <optional>
#include <string>
#include <fmt/format.h>

#include "../command.hpp"
#include "../command_registry.hpp"
#include "../../core/handlers/pathfind_handler.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../packet/message/chat.hpp"

namespace command {
// "/tp <x> <y>" teleports to a tile, "/tp <player>" next to a player.
class TpCommand final : public ICommand {
public:
    explicit TpCommand(core::handlers::PathfindHandler& pathfind)
        : pathfind_{ pathfind }
    {

    }

    [[nodiscard]] std::string_view name() const override { return "tp"; }
    [[nodiscard]] std::string description() const override { return "Teleport to a tile (x y) or a player"; }

    Result execute(const Context& ctx) override
    {
        if (ctx.args.empty()) {
            packet::message::Log log{};
            log.msg = fmt::format("`4Usage: ``{0}tp <x> <y>`` or ``{0}tp <player>`` (``{0}pos`` shows your tile)", ctx.registry.prefix());
            std::ignore = packet::PacketHelper::write(log, ctx.server);
            return Result::InvalidArguments;
        }

        if (ctx.args.size() == 2) {
            if (const auto x{ number(ctx.args[0]) }, y{ number(ctx.args[1]) }; x && y) {
                return pathfind_.teleport_to_tile(*x, *y) ? Result::Success : Result::Failed;
            }
        }

        std::string who{ ctx.args.front() };
        for (std::size_t i{ 1 }; i < ctx.args.size(); ++i) {
            who += ' ' + ctx.args[i];
        }
        return pathfind_.teleport_to_player(who) ? Result::Success : Result::Failed;
    }

private:
    [[nodiscard]] static std::optional<int> number(const std::string& text)
    {
        int value{ 0 };
        const auto [ptr, ec]{ std::from_chars(text.data(), text.data() + text.size(), value) };
        return ec == std::errc{} && ptr == text.data() + text.size() ? std::optional{ value } : std::nullopt;
    }

    core::handlers::PathfindHandler& pathfind_;
};
}
