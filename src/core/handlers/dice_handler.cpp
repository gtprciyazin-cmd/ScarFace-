#include "dice_handler.hpp"

#include <string>
#include <tuple>

#include <fmt/format.h>

#include "../../item/item_database.hpp"
#include "../../packet/game/message.hpp"
#include "../../packet/game/world.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../player/player.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    constexpr auto damage_type{ event::packet_event_type(packet::PacketId::TileApplyDamage) };
}

DiceHandler::DiceHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config)
    : server_{ server }
    , config_{ config }
{
    handle_ = event::ScopedHandle{
        dispatcher,
        damage_type,
        dispatcher.append_listener(damage_type, [this](const event::Event& e) { on_damage(e); })
    };
}

void DiceHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().instant_dice = enabled;
    std::ignore = config_.save();
}

void DiceHandler::on_damage(const event::Event& event)
{
    if (!is_enabled()) {
        return;
    }

    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::TileApplyDamage>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::TileApplyDamage>() };
    if (!pkt) {
        return;
    }

    auto& world{ world::World::instance() };
    const auto& map{ world.get_tile_map() };
    const auto& size{ map.get_size() };
    if (pkt->int_x < 0 || pkt->int_y < 0 || pkt->int_x >= size.x || pkt->int_y >= size.y) {
        return;
    }

    const auto index{ static_cast<std::size_t>(pkt->int_x) + static_cast<std::size_t>(pkt->int_y) * static_cast<std::size_t>(size.x) };
    if (index >= map.get_tiles().size()) {
        return;
    }

    const auto foreground{ map.get_tiles()[index].foreground };
    const auto* item{ item::ItemDatabase::instance().get_item(foreground) };
    if (!item || item->item_type != item::ItemType::Dice) {
        return;
    }

    const auto now{ std::chrono::steady_clock::now() };
    if (static_cast<std::int64_t>(index) == last_tile_ && pkt->value == last_value_ && now - last_time_ < std::chrono::seconds{ 1 }) {
        return;
    }
    last_tile_ = static_cast<std::int64_t>(index);
    last_value_ = pkt->value;
    last_time_ = now;

    // The Dice Block counts faces from 0.
    const int face{ foreground == DICE_BLOCK ? pkt->value + 1 : pkt->value };
    if (foreground == DICE_BLOCK && face > 6) {
        return;
    }

    std::string who{ "Someone" };
    std::int32_t bubble_net_id{ world.get_local_net_id() };
    if (const auto player{ world.get_player(pkt->puncher_net_id).lock() }) {
        who = player->name();
        bubble_net_id = player->net_id();
    }

    packet::message::Log log{};
    log.msg = fmt::format("`9[Dice]`` `w{}`` will roll `2{}``", who, face);
    std::ignore = packet::PacketHelper::write(log, server_);

    if (bubble_net_id >= 0) {
        packet::game::OnTalkBubble bubble{};
        bubble.net_id = bubble_net_id;
        bubble.text = fmt::format("`9[Dice]`` `2{}``", face);
        std::ignore = packet::PacketHelper::write(bubble, server_);
    }
}
}
