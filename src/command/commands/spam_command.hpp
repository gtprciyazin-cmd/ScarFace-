#pragma once
#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <fmt/format.h>

#include "../command.hpp"
#include "../command_registry.hpp"
#include "../../core/handlers/spam_handler.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../packet/message/chat.hpp"

namespace command {
// "/spam on|off|status", "/spam yazi <message>", "/spam sure <seconds>".
// Typed alone it opens the spam menu (see Core::register_feature_commands).
class SpamCommand final : public ICommand {
public:
    explicit SpamCommand(core::handlers::SpamHandler& handler)
        : handler_{ handler }
    {

    }

    [[nodiscard]] std::string_view name() const override { return "spam"; }
    [[nodiscard]] std::string description() const override { return "Say a message in chat every few seconds"; }

    Result execute(const Context& ctx) override
    {
        using core::handlers::SpamHandler;

        std::string sub_cmd{ ctx.args.empty() ? "" : ctx.args[0] };
        std::ranges::transform(sub_cmd, sub_cmd.begin(), [](const unsigned char c) { return std::tolower(c); });

        if (sub_cmd.empty() || sub_cmd == "on" || sub_cmd == "off") {
            handler_.set_enabled(sub_cmd.empty() ? !handler_.is_enabled() : sub_cmd == "on");
            send_status(ctx);
            return Result::Success;
        }

        if (sub_cmd == "status") {
            send_status(ctx);
            return Result::Success;
        }

        if (sub_cmd == "yazi" || sub_cmd == "text") {
            // Everything after the sub command, spacing kept as typed.
            const auto& raw{ ctx.raw_input };
            const auto start{ raw.find(ctx.args[0], 1 + name().size()) + ctx.args[0].size() };
            const auto& text{ handler_.set_text(start < raw.size() ? std::string_view{ raw }.substr(start) : std::string_view{}) };
            send_log(ctx, text.empty() ? std::string{ "`4Spam yazisi bos." } : fmt::format("Spam yazisi: ``{}", text));
            return Result::Success;
        }

        if (sub_cmd == "sure" || sub_cmd == "interval") {
            int seconds{ 0 };
            const std::string arg{ ctx.args.size() > 1 ? ctx.args[1] : "" };
            if (
                auto [ptr, ec]{ std::from_chars(arg.data(), arg.data() + arg.size(), seconds) };
                arg.empty() || ec != std::errc{} || ptr != arg.data() + arg.size()
            ) {
                send_log(ctx, fmt::format(
                    "`4Kullanim: ``{}spam sure <{}-{}>",
                    ctx.registry.prefix(),
                    SpamHandler::MIN_INTERVAL_S,
                    SpamHandler::MAX_INTERVAL_S
                ));
                return Result::InvalidArguments;
            }

            send_log(ctx, fmt::format("Spam araligi: {} saniye", handler_.set_interval(seconds)));
            return Result::Success;
        }

        send_log(ctx, fmt::format(
            "`4Kullanim: ``{0}spam [on|off|status], {0}spam yazi <mesaj>, {0}spam sure <saniye>",
            ctx.registry.prefix()
        ));
        return Result::InvalidArguments;
    }

private:
    void send_status(const Context& ctx) const
    {
        send_log(ctx, fmt::format(
            "Spam {}`` ({} sn): {}",
            handler_.is_enabled() ? "`2ON" : "`4OFF",
            handler_.get_interval(),
            handler_.get_text().empty() ? "`4yazi yok``" : handler_.get_text()
        ));
    }

    static void send_log(const Context& ctx, const std::string& msg)
    {
        packet::message::Log log_pkt{};
        log_pkt.msg = msg;
        std::ignore = packet::PacketHelper::write(log_pkt, ctx.server);
    }

    core::handlers::SpamHandler& handler_;
};
}
