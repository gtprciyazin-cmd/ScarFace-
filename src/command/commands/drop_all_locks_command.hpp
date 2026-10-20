#pragma once
#include <string>

#include "../command.hpp"
#include "../../core/handlers/drop_handler.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../packet/message/chat.hpp"

namespace command {
// "/daw": drops every World, Diamond and Blue Gem Lock in the inventory.
class DropAllLocksCommand final : public ICommand {
public:
    explicit DropAllLocksCommand(core::handlers::DropHandler& handler)
        : handler_{ handler }
    {

    }

    [[nodiscard]] std::string_view name() const override { return "daw"; }
    [[nodiscard]] std::string description() const override { return "Drop all World, Diamond and Blue Gem Locks"; }

    Result execute(const Context& ctx) override
    {
        switch (handler_.request_drop_all_locks()) {
        case core::handlers::DropHandler::Result::Sent:
        case core::handlers::DropHandler::Result::Converting:
            send_log(ctx, "Dropping all `wWorld``, `1Diamond`` and `eBlue Gem`` Locks...");
            return Result::Success;
        case core::handlers::DropHandler::Result::NotInWorld:
            send_log(ctx, "`4Error: ``You are not in a world");
            return Result::Failed;
        case core::handlers::DropHandler::Result::NotConnected:
        case core::handlers::DropHandler::Result::InvalidAmount:
            break;
        }

        send_log(ctx, "`4Error: ``Not connected to the server");
        return Result::Failed;
    }

private:
    static void send_log(const Context& ctx, const std::string& msg)
    {
        packet::message::Log log_pkt{};
        log_pkt.msg = msg;
        std::ignore = packet::PacketHelper::write(log_pkt, ctx.server);
    }

    core::handlers::DropHandler& handler_;
};
}
