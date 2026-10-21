#include "auto_access_handler.hpp"

#include <tuple>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../../packet/game/dialog.hpp"
#include "../../packet/game/message.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/message/raw_text.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    constexpr auto console_type{ event::packet_event_type(packet::PacketId::OnConsoleMessage) };
}

AutoAccessHandler::AutoAccessHandler(
    event::Dispatcher& dispatcher,
    network::Client& client,
    network::Server& server,
    DialogInterceptor& dialogs,
    Config& config
)
    : client_{ client }
    , server_{ server }
    , dialogs_{ dialogs }
    , config_{ config }
{
    handle_ = event::ScopedHandle{
        dispatcher,
        console_type,
        dispatcher.append_listener(console_type, [this](const event::Event& e) { on_console_message(e); })
    };
}

void AutoAccessHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().auto_access = enabled;
    std::ignore = config_.save();
}

void AutoAccessHandler::on_console_message(const event::Event& event)
{
    if (!is_enabled()) {
        return;
    }

    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::OnConsoleMessage>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::OnConsoleMessage>() };
    if (!pkt) {
        return;
    }

    const auto offer{ utils::game_text::parse_access_offer(pkt->text) };
    if (!offer || world::World::instance().get_local_net_id() < 0) {
        return;
    }

    const auto now{ std::chrono::steady_clock::now() };
    if (offer->owner == last_owner_ && now - last_offer_ < DUPLICATE_WINDOW) {
        return;
    }
    last_owner_ = offer->owner;
    last_offer_ = now;

    // About as fast as a player who was waiting for it.
    const auto delay{ std::chrono::milliseconds{ std::uniform_int_distribution{ 400, 900 }(random_) } };
    dialogs_.run_later(delay, [this, offer = *offer] { accept(offer); });
}

void AutoAccessHandler::accept(const utils::game_text::AccessOffer& offer)
{
    const auto net_id{ world::World::instance().get_local_net_id() };
    if (net_id < 0 || !send_text(fmt::format("action|wrench\n|netid|{}", net_id))) {
        return;
    }

    dialogs_.expect(
        "popup",
        [this, net_id, offer](const packet::game::DialogInfo& dialog) {
            if (dialog.get_embed_data("netID") != std::to_string(net_id)) {
                return false; // Someone else's menu.
            }

            if (!dialog.has_button("acceptlock")) {
                send_log(fmt::format("`4Auto Access: ``no pending offer from `w{}`` any more.", offer.owner));
                return true;
            }

            send_text(packet::game::make_dialog_return(dialog, { { "buttonClicked", "acceptlock" } }));
            dialogs_.expect(
                "acceptaccess",
                [this, offer](const packet::game::DialogInfo& confirm) {
                    send_text(packet::game::make_dialog_return(confirm, {}));
                    send_log(fmt::format("`2Auto Access: ``accepted `w{}`` from `w{}``", offer.lock, offer.owner));
                    spdlog::info("Auto access: accepted {} from {}", offer.lock, offer.owner);
                    return true;
                },
                DIALOG_TIMEOUT,
                [this] { send_log("`4Auto Access: ``the server did not ask to confirm."); }
            );
            return true;
        },
        DIALOG_TIMEOUT,
        [this] { send_log("`4Auto Access: ``your wrench menu did not open."); }
    );
}

bool AutoAccessHandler::send_text(const std::string_view text) const
{
    return client_.write(packet::message::make_raw_text(packet::NET_MESSAGE_GENERIC_TEXT, text));
}

void AutoAccessHandler::send_log(const std::string& msg) const
{
    packet::message::Log log{};
    log.msg = msg;
    std::ignore = packet::PacketHelper::write(log, server_);
}
}
