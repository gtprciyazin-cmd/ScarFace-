#include "entrance_color_handler.hpp"

#include <tuple>

#include "../../item/item_database.hpp"
#include "../../packet/game/world.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../utils/byte_stream.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    constexpr auto map_data_type{ event::packet_event_type(packet::PacketId::SendMapData) };
    constexpr auto tile_update_type{ event::packet_event_type(packet::PacketId::SendTileUpdateData) };
}

EntranceColorHandler::EntranceColorHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config)
    : server_{ server }
    , config_{ config }
{
    // Lowest: everything else (world parsing, scripts) sees the server's bytes
    // first, and cancelling here does not hide the packet from them.
    map_handle_ = event::ScopedHandle{
        dispatcher,
        map_data_type,
        dispatcher.append_listener(map_data_type, [this](const event::Event& e) { on_map_data(e); }, event::Priority::Lowest)
    };
    update_handle_ = event::ScopedHandle{
        dispatcher,
        tile_update_type,
        dispatcher.append_listener(tile_update_type, [this](const event::Event& e) { on_tile_update(e); }, event::Priority::Lowest)
    };
}

void EntranceColorHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().public_gates = enabled;
    std::ignore = config_.save();

    packet::message::Log log{};
    log.msg = "`oRe-enter the world to update the entrance colours.";
    std::ignore = packet::PacketHelper::write(log, server_);
}

std::optional<bool> EntranceColorHandler::is_public_entrance(const world::Tile& tile)
{
    const auto* item{ item::ItemDatabase::instance().get_item(tile.foreground) };
    if (!item || item->item_type != item::ItemType::Gateway) {
        return std::nullopt;
    }

    // Set from the gate's "Is open to public" checkbox.
    return world::has_flag(tile.flag, world::TileFlag::Public);
}

void EntranceColorHandler::on_map_data(const event::Event& event)
{
    if (!is_enabled()) {
        return;
    }

    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::SendMapData>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::SendMapData>() };
    if (!pkt || pkt->extra.empty()) {
        return;
    }

    // WorldHandler parsed this very packet; tiles it could not read are left alone.
    const auto& map{ world::World::instance().get_tile_map() };
    auto data{ pkt->extra };
    bool changed{ false };
    for (std::size_t i{ 0 }; i < map.get_tiles().size(); ++i) {
        const auto state{ is_public_entrance(map.get_tiles()[i]) };
        const auto bytes{ map.get_tile_bytes(i) };
        if (state && bytes && bytes->second <= data.size()) {
            changed |= world::paint_tile_record(data, bytes->first, *state);
        }
    }

    if (!changed) {
        return;
    }

    packet::game::SendMapData painted{};
    painted.game_packet = pkt->game_packet;
    painted.extra = std::move(data);
    if (packet::PacketHelper::write(painted, server_)) {
        evt->cancel();
    }
}

void EntranceColorHandler::on_tile_update(const event::Event& event)
{
    if (!is_enabled()) {
        return;
    }

    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::SendTileUpdateData>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::SendTileUpdateData>() };
    if (!pkt || pkt->extra.empty()) {
        return;
    }

    world::Tile tile{};
    utils::ByteStream<> bs{ pkt->extra.data(), pkt->extra.size() };
    std::ignore = tile.serialize(bs, world::World::instance().get_version());

    const auto state{ is_public_entrance(tile) };
    auto data{ pkt->extra };
    if (!state || !world::paint_tile_record(data, 0, *state)) {
        return;
    }

    packet::game::SendTileUpdateData painted{};
    painted.game_packet = pkt->game_packet;
    painted.extra = std::move(data);
    if (packet::PacketHelper::write(painted, server_)) {
        evt->cancel();
    }
}
}
