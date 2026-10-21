#include "name_tag_handler.hpp"

#include <algorithm>
#include <ranges>
#include <tuple>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../../packet/game/player.hpp"
#include "../../packet/game/world.hpp"
#include "../../packet/message/exit.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../utils/chat_text.hpp"
#include "../../world/player_finder.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    // The whole value of a "key|value" line in OnSpawn's text, pipes included
    // ("country|us|showGuild|maxLevel" -> "us|showGuild|maxLevel").
    [[nodiscard]] std::string line_value(const std::string_view text, const std::string_view key)
    {
        for (std::size_t start{ 0 }; start < text.size();) {
            const auto end{ std::min(text.find('\n', start), text.size()) };
            const auto line{ text.substr(start, end - start) };
            if (line.size() > key.size() && line.starts_with(key) && line[key.size()] == '|') {
                return std::string{ line.substr(key.size() + 1) };
            }
            start = end + 1;
        }
        return {};
    }

    template <packet::PacketId Id>
    [[nodiscard]] const event::TypedPacketEvent<Id>* client_bound(const event::Event& event)
    {
        const auto* evt{ dynamic_cast<const event::TypedPacketEvent<Id>*>(&event) };
        return evt && evt->direction == event::Direction::ClientBound ? evt : nullptr;
    }
}

NameTagHandler::NameTagHandler(
    event::Dispatcher& dispatcher,
    std::shared_ptr<Scheduler> scheduler,
    network::Client& client,
    network::Server& server,
    Config& config
)
    : scheduler_{ std::move(scheduler) }
    , client_{ client }
    , server_{ server }
    , config_{ config }
{
    const auto listen{ [this, &dispatcher](const packet::PacketId id, void (NameTagHandler::*handler)(const event::Event&)) {
        const auto type{ event::packet_event_type(id) };
        handles_.emplace_back(
            dispatcher,
            type,
            dispatcher.append_listener(type, [this, handler](const event::Event& e) { (this->*handler)(e); })
        );
    } };
    listen(packet::PacketId::OnSpawn, &NameTagHandler::on_spawn);
    listen(packet::PacketId::OnNameChanged, &NameTagHandler::on_name_changed);
    listen(packet::PacketId::OnCountryState, &NameTagHandler::on_country_state);
    listen(packet::PacketId::OnRemove, &NameTagHandler::on_remove);
    listen(packet::PacketId::JoinRequest, &NameTagHandler::on_join);

    if (is_enabled()) {
        start_ping_timer();
    }
}

NameTagHandler::~NameTagHandler()
{
    stop_ping_timer();
}

void NameTagHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().ping_in_name = enabled;
    std::ignore = config_.save();

    if (enabled) {
        start_ping_timer();
    }
    else {
        stop_ping_timer();
    }
    refresh(local_net_id_);
}

void NameTagHandler::set_spins_enabled(const bool enabled)
{
    config_.get_feature_config().last_spin_names = enabled;
    std::ignore = config_.save();
    refresh_all();
}

void NameTagHandler::start_ping_timer()
{
    if (ping_task_ != INVALID_TASK_ID) {
        return;
    }

    // Scheduler callbacks run on a worker thread; ENet is not thread-safe, so
    // hop back to the main thread before sending the packet.
    Scheduler* const scheduler{ scheduler_.get() };
    ping_task_ = scheduler_->schedule_periodic(
        [this, scheduler] { scheduler->post_to_main_thread([this] { refresh(local_net_id_); }); },
        REFRESH_INTERVAL,
        "ping_name",
        TaskPriority::Low,
        REFRESH_INTERVAL
    );
}

void NameTagHandler::stop_ping_timer()
{
    if (ping_task_ == INVALID_TASK_ID) {
        return;
    }

    scheduler_->cancel_by_tag("ping_name");
    ping_task_ = INVALID_TASK_ID;
}

void NameTagHandler::refresh_later(const std::int32_t net_id, const std::chrono::milliseconds delay)
{
    Scheduler* const scheduler{ scheduler_.get() };
    scheduler_->schedule_delayed(
        [this, scheduler, net_id] { scheduler->post_to_main_thread([this, net_id] { refresh(net_id); }); },
        delay,
        TASK_TAG
    );
}

bool NameTagHandler::in_world(const std::int32_t net_id) const
{
    const auto& world{ world::World::instance() };
    return net_id >= 0 && (net_id == world.get_local_net_id() || !world.get_player(net_id).expired());
}

std::string NameTagHandler::display_name(const std::int32_t net_id, const Player& player) const
{
    std::string suffix{};
    if (spins_enabled()) {
        if (const auto it{ spins_.find(world::normalize_player_name(player.base_name)) }; it != spins_.end()) {
            suffix = it->second;
        }
    }

    // 0 means not connected; show nothing rather than "(0 ms)".
    if (net_id == local_net_id_ && is_enabled()) {
        if (const auto ping{ client_.round_trip_time() }; ping > 0) {
            suffix += (suffix.empty() ? "" : " ") + utils::ping_suffix(ping);
        }
    }

    return utils::decorate_name(player.base_name, suffix);
}

void NameTagHandler::refresh(const std::int32_t net_id)
{
    const auto it{ players_.find(net_id) };
    if (it == players_.end() || it->second.base_name.empty() || !client_.is_connected() || !in_world(net_id)) {
        return;
    }

    if (const auto name{ display_name(net_id, it->second) }; name != it->second.shown) {
        send_name(net_id, it->second, name);
    }
}

void NameTagHandler::refresh_all()
{
    for (const auto net_id : players_ | std::views::keys) {
        refresh(net_id);
    }
}

void NameTagHandler::send_name(const std::int32_t net_id, Player& player, const std::string& name)
{
    packet::game::OnNameChanged pkt{};
    pkt.net_id = net_id;
    pkt.name = name;
    pkt.variant = packet::PacketVariant{ "OnNameChanged", name, player.title };
    if (packet::PacketHelper::write(pkt, server_)) {
        player.shown = name;
        restore_badges(net_id, player);
    }
}

void NameTagHandler::restore_badges(const std::int32_t net_id, const Player& player) const
{
    if (!player.shown.ends_with(" of Legend``") || player.country.empty()) {
        return;
    }

    packet::game::OnCountryState pkt{};
    pkt.net_id = net_id;
    pkt.country = player.country;
    std::ignore = packet::PacketHelper::write(pkt, server_);
}

bool NameTagHandler::rename(const std::string& name)
{
    const auto it{ players_.find(local_net_id_) };
    if (it == players_.end() || !in_world(local_net_id_)) {
        return false;
    }

    it->second.base_name = name;
    send_name(local_net_id_, it->second, display_name(local_net_id_, it->second));
    return true;
}

void NameTagHandler::set_last_spin(const std::int32_t net_id, const int number, const std::string& colour_code, const std::chrono::milliseconds delay)
{
    const auto it{ players_.find(net_id) };
    if (it == players_.end()) {
        return;
    }

    spins_[world::normalize_player_name(it->second.base_name)] = fmt::format("`w[{}{}`w]", colour_code, number);
    if (spins_enabled()) {
        // When the wheel stops, like the server's own message.
        refresh_later(net_id, std::max(delay, std::chrono::milliseconds{ 0 }));
    }
}

void NameTagHandler::on_spawn(const event::Event& event)
{
    const auto* evt{ client_bound<packet::PacketId::OnSpawn>(event) };
    const auto pkt{ evt ? evt->get<packet::game::OnSpawn>() : nullptr };
    if (!pkt) {
        return;
    }

    // A new avatar is created with the plain name; keep the title and country
    // lines whole (the parsed fields stop at the first pipe).
    const auto text{ pkt->variant.get<std::string>(1) };
    auto& player{ players_[pkt->net_id] };
    player = Player{ pkt->name, line_value(text, "titleIcon"), line_value(text, "country"), pkt->name };
    if (pkt->type == "local") {
        local_net_id_ = pkt->net_id;
    }

    if (display_name(pkt->net_id, player) != player.shown) {
        refresh_later(pkt->net_id, SPAWN_DELAY);
    }
}

void NameTagHandler::on_name_changed(const event::Event& event)
{
    const auto* evt{ client_bound<packet::PacketId::OnNameChanged>(event) };
    const auto pkt{ evt ? evt->get<packet::game::OnNameChanged>() : nullptr };
    if (!pkt) {
        return;
    }

    const auto it{ players_.find(pkt->net_id) };
    if (it == players_.end() || !in_world(pkt->net_id)) {
        return;
    }

    // The server renamed them (world lock, title, nick...): remember the new
    // name, and show it with the tags straight away instead of flickering.
    // Without a title argument the client removes the title, so mirror that.
    auto& player{ it->second };
    player.base_name = pkt->name;
    player.title = pkt->variant.size() > 2 ? pkt->variant.get<std::string>(2) : std::string{};
    player.shown = pkt->name;

    const auto name{ display_name(pkt->net_id, player) };
    if (name == pkt->name) {
        return;
    }

    auto renamed{ *pkt };
    renamed.name = name;
    if (packet::PacketHelper::write(renamed, server_)) {
        player.shown = name;
        evt->cancel();
        restore_badges(pkt->net_id, player);
    }
}

void NameTagHandler::on_country_state(const event::Event& event)
{
    const auto* evt{ client_bound<packet::PacketId::OnCountryState>(event) };
    if (const auto pkt{ evt ? evt->get<packet::game::OnCountryState>() : nullptr }) {
        if (const auto it{ players_.find(pkt->net_id) }; it != players_.end()) {
            it->second.country = pkt->country;
        }
    }
}

void NameTagHandler::on_remove(const event::Event& event)
{
    const auto* evt{ client_bound<packet::PacketId::OnRemove>(event) };
    if (const auto pkt{ evt ? evt->get<packet::game::OnRemove>() : nullptr }) {
        players_.erase(pkt->net_id);
    }
}

void NameTagHandler::on_join(const event::Event& event)
{
    // Net ids are handed out again in every world.
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::JoinRequest>*>(&event) };
    if (evt && evt->direction == event::Direction::ServerBound) {
        players_.clear();
        local_net_id_ = -1;
    }
}
}
