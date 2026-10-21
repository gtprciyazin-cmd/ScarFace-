#pragma once
#include <functional>
#include <string>
#include <tuple>
#include <fmt/format.h>

#include "../command.hpp"
#include "../command_registry.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../packet/game/player.hpp"
#include "../../packet/message/chat.hpp"
#include "../../world/world.hpp"

namespace command {
class NickCommand final : public ICommand {
public:
    // Renames the local player and returns false when that is not possible.
    using Renamer = std::function<bool(const std::string&)>;

    NickCommand() = default;

    // With a renamer the name goes through it (the ping display keeps it as
    // the base name), otherwise it is sent to the game directly.
    explicit NickCommand(Renamer renamer)
        : renamer_{ std::move(renamer) }
    {

    }

    [[nodiscard]] std::string_view name() const override { return "nick"; }
    [[nodiscard]] std::string description() const override { return "Change your display name"; }

    Result execute(const Context& ctx) override
    {
        if (ctx.args.empty()) {
            packet::message::Log log{};
            log.msg = fmt::format("`4Usage: ``{}nick <nickname>", ctx.registry.prefix());
            std::ignore = packet::PacketHelper::write(log, ctx.server);
            return Result::InvalidArguments;
        }

        std::string name{ ctx.args.front() };
        for (size_t i{ 1 }; i < ctx.args.size(); ++i) {
            name += ' ' + ctx.args[i];
        }

        if (renamer_) {
            if (!renamer_(name)) {
                packet::message::Log log{};
                log.msg = "`4Enter a world first.";
                std::ignore = packet::PacketHelper::write(log, ctx.server);
                return Result::Failed;
            }
        }
        else {
            packet::game::OnNameChanged pkt{};
            pkt.net_id = world::World::instance().get_local_net_id();
            pkt.name = name;
            std::ignore = packet::PacketHelper::write(pkt, ctx.server);
        }

        packet::message::Log log{};
        log.msg = fmt::format("Display name changed to {}", name);
        std::ignore = packet::PacketHelper::write(log, ctx.server);

        return Result::Success;
    }

private:
    Renamer renamer_;
};
}
