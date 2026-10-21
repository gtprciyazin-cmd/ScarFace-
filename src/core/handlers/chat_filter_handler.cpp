#include "chat_filter_handler.hpp"

#include <tuple>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../../packet/game/message.hpp"
#include "../../packet/generic_packets.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../utils/color_codes.hpp"
#include "../../utils/game_text.hpp"

namespace core::handlers {
ChatFilterHandler::ChatFilterHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config)
    : dispatcher_{ dispatcher }
    , server_{ server }
    , config_{ config }
{
    listen_text<packet::PacketId::OnConsoleMessage, packet::game::OnConsoleMessage>();
    listen_text<packet::PacketId::OnTalkBubble, packet::game::OnTalkBubble>();
    listen_text<packet::PacketId::OnTextOverlay, packet::game::OnTextOverlay>();

    listen_tutorial<packet::PacketId::OnTutorialArrow>();
    listen_tutorial<packet::PacketId::OnPersistentNPCMessage>();
    listen_tutorial<packet::PacketId::OnPersistentNPCMessageInputDependent>();
    listen_tutorial<packet::PacketId::ShowStartFTUEPopup>();
    listen_tutorial<packet::PacketId::OnWorldMenuFTUEMessageRequest>();
}

void ChatFilterHandler::set_hide_private_messages(const bool enabled)
{
    config_.get_feature_config().hide_private_messages = enabled;
    std::ignore = config_.save();
}

void ChatFilterHandler::set_hide_level_text(const bool enabled)
{
    config_.get_feature_config().hide_level_text = enabled;
    std::ignore = config_.save();

    if (enabled) {
        // Take down whatever the tutorial already shows.
        for (const auto* function : { "OnClearAllTutorialArrows", "OnClearNPCMessage" }) {
            packet::GenericVariantPacket clear{};
            clear.variant = packet::PacketVariant{ std::string{ function } };
            std::ignore = packet::PacketHelper::write(clear, server_);
        }
    }
}

template <packet::PacketId Id, typename Packet>
void ChatFilterHandler::listen_text()
{
    // FairlyLow: the observers (roulette history, spam stop, auto access) read
    // the line first; a cancel here only keeps it off the screen, and runs
    // before the Lowest rewriters (spin tags) so a hidden line stays hidden.
    constexpr auto type{ event::packet_event_type(Id) };
    handles_.emplace_back(
        dispatcher_,
        type,
        dispatcher_.append_listener(type, [this](const event::Event& event) {
            const auto* evt{ dynamic_cast<const event::TypedPacketEvent<Id>*>(&event) };
            if (!evt || evt->direction != event::Direction::ClientBound) {
                return;
            }

            if (const auto pkt{ evt->template get<Packet>() }; pkt && filter(pkt->text)) {
                evt->cancel();
            }
        }, event::Priority::FairlyLow)
    );
}

template <packet::PacketId Id>
void ChatFilterHandler::listen_tutorial()
{
    constexpr auto type{ event::packet_event_type(Id) };
    handles_.emplace_back(
        dispatcher_,
        type,
        dispatcher_.append_listener(type, [this](const event::Event& event) {
            const auto* evt{ dynamic_cast<const event::TypedPacketEvent<Id>*>(&event) };
            if (evt && evt->direction == event::Direction::ClientBound && hides_level_text()) {
                evt->cancel();
            }
        }, event::Priority::Lowest)
    );
}

bool ChatFilterHandler::filter(const std::string& text)
{
    if (hides_private_messages()) {
        if (const auto sender{ utils::game_text::private_message_sender(text) }) {
            // Staff names start with @; never miss a moderator.
            if (sender->starts_with('@')) {
                return false;
            }

            hidden_.push_back(text);
            if (hidden_.size() > MAX_HIDDEN) {
                hidden_.pop_front();
            }
            spdlog::info("Chat filter: hid a private message from {}", *sender);
            return true;
        }
    }

    if (hides_level_text() && utils::game_text::is_level_requirement(text)) {
        // The only sign a block will not break; say it once in a while.
        if (const auto now{ std::chrono::steady_clock::now() }; now - last_level_notice_ > LEVEL_NOTICE_INTERVAL) {
            last_level_notice_ = now;
            send_log(fmt::format("`8Hidden: ``{}", utils::strip_color_codes(text)));
        }
        return true;
    }

    return false;
}

void ChatFilterHandler::show_hidden_messages() const
{
    if (hidden_.empty()) {
        send_log("`9[PM]`` No hidden private messages.");
        return;
    }

    send_log(fmt::format("`9[PM]`` {} hidden private messages:", hidden_.size()));
    for (const auto& text : hidden_) {
        // Without the routing prefix, so the console shows it in the main tab.
        auto line{ text };
        if (const auto ct{ line.find("CT:[") }; ct != std::string::npos) {
            if (const auto end{ line.find("]_", ct) }; end != std::string::npos) {
                line.erase(0, end + 2);
            }
        }
        send_log(line);
    }
}

void ChatFilterHandler::send_log(const std::string& msg) const
{
    packet::message::Log log{};
    log.msg = msg;
    std::ignore = packet::PacketHelper::write(log, server_);
}
}
