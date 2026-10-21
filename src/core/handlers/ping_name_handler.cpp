#include "ping_name_handler.hpp"

#include <tuple>

#include <spdlog/spdlog.h>

#include "../../packet/game/player.hpp"
#include "../../packet/game/world.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../utils/chat_text.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    constexpr auto on_spawn_type{ event::packet_event_type(packet::PacketId::OnSpawn) };
    constexpr auto on_name_changed_type{ event::packet_event_type(packet::PacketId::OnNameChanged) };
}

PingNameHandler::PingNameHandler(
    event::Dispatcher& dispatcher,
    std::shared_ptr<Scheduler> scheduler,
    network::Client& client,
    network::Server& server,
    Config& config
)
    : dispatcher_{ dispatcher }
    , scheduler_{ std::move(scheduler) }
    , client_{ client }
    , server_{ server }
    , config_{ config }
{
    spawn_handle_ = event::ScopedHandle{
        dispatcher_,
        on_spawn_type,
        dispatcher_.append_listener(on_spawn_type, [this](const event::Event& e) { on_spawn(e); })
    };
    name_handle_ = event::ScopedHandle{
        dispatcher_,
        on_name_changed_type,
        dispatcher_.append_listener(on_name_changed_type, [this](const event::Event& e) { on_name_changed(e); })
    };

    if (is_enabled()) {
        start();
    }
}

PingNameHandler::~PingNameHandler()
{
    stop();
}

std::string PingNameHandler::format_name(const std::string_view name, const std::uint32_t ping_ms)
{
    return utils::name_with_ping(name, ping_ms);
}

void PingNameHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().ping_in_name = enabled;
    std::ignore = config_.save();

    if (enabled) {
        start();
        refresh();
    }
    else {
        stop();
        restore_name();
    }
}

void PingNameHandler::start()
{
    if (task_id_ != INVALID_TASK_ID) {
        return;
    }

    // Scheduler callbacks run on a worker thread; ENet is not thread-safe, so
    // hop back to the main thread before sending the packet.
    Scheduler* const scheduler{ scheduler_.get() };
    task_id_ = scheduler_->schedule_periodic(
        [this, scheduler] { scheduler->post_to_main_thread([this] { refresh(); }); },
        REFRESH_INTERVAL,
        TASK_TAG,
        TaskPriority::Low,
        REFRESH_INTERVAL
    );
}

void PingNameHandler::stop()
{
    if (task_id_ == INVALID_TASK_ID) {
        return;
    }

    scheduler_->cancel_by_tag(TASK_TAG);
    task_id_ = INVALID_TASK_ID;
}

void PingNameHandler::on_spawn(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::OnSpawn>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::OnSpawn>() };
    if (!pkt || pkt->type != "local") {
        return;
    }

    // A new world: the avatar is created with the plain name again.
    net_id_ = pkt->net_id;
    base_name_ = pkt->name;
    base_title_ = pkt->title_icon;
    shown_name_.clear();

    if (!is_enabled()) {
        return;
    }

    Scheduler* const scheduler{ scheduler_.get() };
    scheduler_->schedule_delayed(
        [this, scheduler] { scheduler->post_to_main_thread([this] { refresh(); }); },
        SPAWN_DELAY,
        TASK_TAG
    );
}

void PingNameHandler::on_name_changed(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::OnNameChanged>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::OnNameChanged>() };
    if (!pkt || net_id_ < 0 || pkt->net_id != net_id_) {
        return;
    }

    // The server renamed us (world lock, title, nick...): remember the new name
    // and show it with the ping straight away instead of flickering. Without
    // a title argument the client removes the title, so mirror that.
    base_name_ = pkt->name;
    base_title_ = pkt->variant.size() > 2 ? pkt->variant.get<std::string>(2) : std::string{};
    if (!is_enabled()) {
        shown_name_.clear();
        return;
    }

    auto renamed{ *pkt };
    renamed.name = display_name();
    if (packet::PacketHelper::write(renamed, server_)) {
        shown_name_ = renamed.name;
        evt->cancel();
    }
}

void PingNameHandler::refresh()
{
    if (!is_enabled() || net_id_ < 0 || base_name_.empty() || !client_.is_connected()) {
        return;
    }

    // Only while the avatar is in the world the name was read in.
    if (world::World::instance().get_local_net_id() != net_id_) {
        return;
    }

    if (const auto name{ display_name() }; name != shown_name_) {
        send_name(name);
    }
}

bool PingNameHandler::rename(const std::string& name)
{
    if (net_id_ < 0 || world::World::instance().get_local_net_id() != net_id_) {
        return false;
    }

    base_name_ = name;
    send_name(is_enabled() ? display_name() : base_name_);
    return true;
}

std::string PingNameHandler::display_name() const
{
    // 0 means not connected; show the plain name rather than "(0 ms)".
    const auto ping{ client_.round_trip_time() };
    return ping == 0 ? base_name_ : format_name(base_name_, ping);
}

void PingNameHandler::restore_name()
{
    if (net_id_ < 0 || base_name_.empty() || shown_name_.empty()) {
        return;
    }

    if (world::World::instance().get_local_net_id() == net_id_) {
        send_name(base_name_);
    }
    shown_name_.clear();
}

void PingNameHandler::send_name(const std::string& name)
{
    packet::game::OnNameChanged pkt{};
    pkt.net_id = net_id_;
    pkt.name = name;
    pkt.variant = packet::PacketVariant{ "OnNameChanged", name, base_title_ };
    if (packet::PacketHelper::write(pkt, server_)) {
        shown_name_ = name;
    }
}
}
