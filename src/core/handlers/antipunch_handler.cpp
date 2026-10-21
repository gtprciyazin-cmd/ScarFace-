#include "antipunch_handler.hpp"

#include <cmath>
#include <cstdlib>
#include <tuple>

#include "../../packet/game/player.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../player/player.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    constexpr auto state_type{ event::packet_event_type(packet::PacketId::State) };
    constexpr std::uint32_t ON_PUNCHED{ packet::PACKET_FLAG_ON_PUNCHED };
}

AntipunchHandler::AntipunchHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config)
    : server_{ server }
    , config_{ config }
{
    // Lowest: WorldHandler tracks the puncher's position from this packet first.
    handle_ = event::ScopedHandle{
        dispatcher,
        state_type,
        dispatcher.append_listener(state_type, [this](const event::Event& e) { on_state(e); }, event::Priority::Lowest)
    };
}

void AntipunchHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().antipunch = enabled;
    std::ignore = config_.save();
}

bool AntipunchHandler::punch_reaches(const int tile_x, const int tile_y, const float px, const float py)
{
    // The avatar box is 20 x 30 pixels from its position.
    const auto my_x{ static_cast<int>(std::floor((px + 10.0f) / 32.0f)) };
    const auto my_y{ static_cast<int>(std::floor((py + 15.0f) / 32.0f)) };
    return std::abs(tile_x - my_x) <= 1 && std::abs(tile_y - my_y) <= 1;
}

void AntipunchHandler::on_state(const event::Event& event)
{
    if (!is_enabled()) {
        return;
    }

    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::State>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::State>() };
    auto& world{ world::World::instance() };
    if (!pkt || (static_cast<std::uint32_t>(pkt->game_packet.flags.value) & ON_PUNCHED) == 0 || pkt->net_id == world.get_local_net_id()) {
        return;
    }

    const auto me{ world.get_local_player().lock() };
    if (!me || !punch_reaches(pkt->game_packet.int_x, pkt->game_packet.int_y, static_cast<float>(me->position().x), static_cast<float>(me->position().y))) {
        return;
    }

    // Same movement and swing, but at no tile, so it cannot hit us.
    auto harmless{ *pkt };
    harmless.game_packet.int_x = -1;
    harmless.game_packet.int_y = -1;
    if (packet::PacketHelper::write(harmless, server_)) {
        evt->cancel();
    }
}
}
