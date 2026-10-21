#include "pathfind_handler.hpp"

#include <cstdint>
#include <tuple>

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "../../packet/game/player.hpp"
#include "../../packet/game/world.hpp"
#include "../../packet/generic_packets.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../packet/packet_variant.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/message/wrench.hpp"
#include "../../player/player.hpp"
#include "../../world/player_finder.hpp"
#include "../../world/world.hpp"
#include "hotkey_handler.hpp"

namespace core::handlers {
namespace {
    constexpr auto tile_change_type{ event::packet_event_type(packet::PacketId::TileChangeRequest) };
    constexpr auto wrench_type{ event::packet_event_type(packet::PacketId::Wrench) };
}

PathfindHandler::PathfindHandler(event::Dispatcher& dispatcher, network::Client& client, network::Server& server, Config& config)
    : dispatcher_{ dispatcher }
    , client_{ client }
    , server_{ server }
    , config_{ config }
{
    handle_ = event::ScopedHandle{
        dispatcher_,
        tile_change_type,
        dispatcher_.append_listener(tile_change_type, [this](const event::Event& e) { on_tile_change(e); })
    };
    // Before QuickWrenchHandler, which would otherwise pull/kick the player.
    wrench_handle_ = event::ScopedHandle{
        dispatcher_,
        wrench_type,
        dispatcher_.append_listener(wrench_type, [this](const event::Event& e) { on_wrench(e); }, event::Priority::FairlyHigh)
    };
}

void PathfindHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().pathfind = enabled;
    std::ignore = config_.save();
}

void PathfindHandler::on_tile_change(const event::Event& event)
{
    if (!is_enabled()) {
        return;
    }

    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::TileChangeRequest>*>(&event) };
    if (!evt || evt->direction != event::Direction::ServerBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::TileChangeRequest>() };
    if (!pkt) {
        return;
    }

    if (world::World::instance().get_local_net_id() < 0) {
        return; // Not in a world yet.
    }

    // Only Shift + Click teleports; plain clicks punch and place as usual.
    if (!HotkeyHandler::shift_held() || !HotkeyHandler::growtopia_focused()) {
        return;
    }

    // Take over the click: no punch/place, teleport instead.
    evt->cancel();
    std::ignore = teleport_to_tile(pkt->int_x, pkt->int_y);
}

void PathfindHandler::on_wrench(const event::Event& event)
{
    if (!is_enabled() || !HotkeyHandler::shift_held() || !HotkeyHandler::growtopia_focused()) {
        return;
    }

    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::Wrench>*>(&event) };
    if (!evt || evt->direction != event::Direction::ServerBound) {
        return;
    }

    const auto pkt{ evt->get<packet::message::Wrench>() };
    const auto& world{ world::World::instance() };
    if (!pkt || pkt->net_id == world.get_local_net_id()) {
        return;
    }

    if (const auto player{ world.get_player(pkt->net_id).lock() }) {
        // Instead of their profile: go to them.
        evt->cancel();
        teleport(static_cast<float>(player->position().x), static_cast<float>(player->position().y));
        send_log(fmt::format("`9[Pathfind]`` Teleported to `w{}``", player->name()));
    }
}

bool PathfindHandler::teleport_to_tile(const int tile_x, const int tile_y)
{
    auto& world{ world::World::instance() };
    if (world.get_local_net_id() < 0) {
        send_log("`4Enter a world first.");
        return false;
    }

    const auto& size{ world.get_tile_map().get_size() };
    if (tile_x < 0 || tile_y < 0 || tile_x >= size.x || tile_y >= size.y) {
        send_log(fmt::format("`4Outside the world (0-{}, 0-{}).", size.x - 1, size.y - 1));
        return false;
    }

    teleport(static_cast<float>(tile_x) * TILE_SIZE, static_cast<float>(tile_y) * TILE_SIZE);
    return true;
}

bool PathfindHandler::teleport_to_player(const std::string_view name)
{
    const auto& world{ world::World::instance() };
    if (world.get_local_net_id() < 0) {
        send_log("`4Enter a world first.");
        return false;
    }

    const auto matches{ world::find_players_by_name(world.get_players(), name, world.get_local_net_id()) };
    if (matches.empty()) {
        send_log(fmt::format("`4No player named `w{}`` here.", name));
        return false;
    }

    const auto player{ world.get_player(matches.front().net_id).lock() };
    if (!player) {
        return false;
    }

    teleport(static_cast<float>(player->position().x), static_cast<float>(player->position().y));
    send_log(fmt::format("`9[Pathfind]`` Teleported to `w{}``", matches.front().name));
    return true;
}

void PathfindHandler::show_position() const
{
    const auto player{ world::World::instance().get_local_player().lock() };
    if (!player) {
        send_log("`4Enter a world first.");
        return;
    }

    send_log(fmt::format(
        "`9[Pathfind]`` You are on tile `w{}, {}``",
        static_cast<int>((static_cast<float>(player->position().x) + 10.0f) / TILE_SIZE),
        static_cast<int>((static_cast<float>(player->position().y) + 15.0f) / TILE_SIZE)
    ));
}

void PathfindHandler::teleport(const float px, const float py)
{
    const auto net_id{ world::World::instance().get_local_net_id() };

    // Snap the local player on the client.
    packet::GenericVariantPacket set_pos{};
    set_pos.game_packet.net_id = static_cast<std::uint32_t>(net_id);
    set_pos.variant = packet::PacketVariant{ "OnSetPos", glm::vec2{ px, py } };
    std::ignore = packet::PacketHelper::write(set_pos, server_);

    // Report the new position to the server the way the game reports a move:
    // net id 0 (the server knows who sent it), standing, no tile action.
    packet::game::State state{};
    state.net_id = 0;
    state.pos_x = px;
    state.pos_y = py;
    state.game_packet.flags.value = packet::PACKET_FLAG_ON_SOLID;
    state.game_packet.int_x = -1;
    state.game_packet.int_y = -1;
    std::ignore = packet::PacketHelper::write(state, client_);

    // Keep the proxy's own idea of where we are in sync.
    if (const auto player{ world::World::instance().get_local_player().lock() }) {
        player->set_position(glm::i32vec2{ static_cast<int>(px), static_cast<int>(py) });
    }

    spdlog::info("Pathfind: teleport to px {:.0f},{:.0f}", px, py);
}

void PathfindHandler::send_log(const std::string& msg) const
{
    packet::message::Log log{};
    log.msg = msg;
    std::ignore = packet::PacketHelper::write(log, server_);
}
}
