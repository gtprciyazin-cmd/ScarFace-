#include "zoom_handler.hpp"

#include <algorithm>
#include <tuple>

#include "../../packet/game/world.hpp"
#include "../../packet/generic_packets.hpp"
#include "../../packet/packet_helper.hpp"

namespace core::handlers {
namespace {
    constexpr auto spawn_type{ event::packet_event_type(packet::PacketId::OnSpawn) };
}

ZoomHandler::ZoomHandler(event::Dispatcher& dispatcher, std::shared_ptr<Scheduler> scheduler, network::Server& server, Config& config)
    : scheduler_{ std::move(scheduler) }
    , server_{ server }
    , config_{ config }
{
    handle_ = event::ScopedHandle{
        dispatcher,
        spawn_type,
        dispatcher.append_listener(spawn_type, [this](const event::Event& e) { on_spawn(e); })
    };
}

void ZoomHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().zoom_out = enabled;
    std::ignore = config_.save();
    apply();
}

float ZoomHandler::set_level(const float level)
{
    auto& features{ config_.get_feature_config() };
    features.zoom_level = std::clamp(level, MIN_LEVEL, MAX_LEVEL);
    features.zoom_out = true;
    std::ignore = config_.save();
    apply();
    return features.zoom_level;
}

void ZoomHandler::on_spawn(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::OnSpawn>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound || !is_enabled()) {
        return;
    }

    if (const auto pkt{ evt->get<packet::game::OnSpawn>() }; !pkt || pkt->type != "local") {
        return;
    }

    Scheduler* const scheduler{ scheduler_.get() };
    scheduler_->schedule_delayed(
        [this, scheduler] { scheduler->post_to_main_thread([this] { apply(); }); },
        JOIN_DELAY,
        "zoom"
    );
}

void ZoomHandler::apply() const
{
    zoom_to(is_enabled() ? get_level() : DEFAULT_ZOOM);
}

void ZoomHandler::zoom_to(const float level) const
{
    packet::GenericVariantPacket zoom{};
    zoom.variant = packet::PacketVariant{ std::string{ "OnZoomCamera" }, level, std::uint32_t{ 400 } };
    std::ignore = packet::PacketHelper::write(zoom, server_);
}
}
