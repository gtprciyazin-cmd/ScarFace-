#include "door_id_handler.hpp"

#include <tuple>

#include <fmt/format.h>

#include "../../item/item_database.hpp"
#include "../../packet/game/server.hpp"
#include "../../packet/game/world.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/message/exit.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    constexpr auto activate_type{ event::packet_event_type(packet::PacketId::TileActivateRequest) };
    constexpr auto redirect_type{ event::packet_event_type(packet::PacketId::OnSendToServer) };
    constexpr auto join_type{ event::packet_event_type(packet::PacketId::JoinRequest) };
    constexpr auto spawn_type{ event::packet_event_type(packet::PacketId::OnSpawn) };

    [[nodiscard]] std::string trim(std::string text)
    {
        const auto first{ text.find_first_not_of(" \t\r\n") };
        if (first == std::string::npos) {
            return {};
        }
        text.erase(0, first);
        text.erase(text.find_last_not_of(" \t\r\n") + 1);
        return text;
    }

    [[nodiscard]] bool is_door(const item::ItemType type)
    {
        return type == item::ItemType::UserDoor || type == item::ItemType::Portal || type == item::ItemType::Sungate;
    }
}

DoorIdHandler::DoorIdHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config)
    : server_{ server }
    , config_{ config }
{
    activate_handle_ = event::ScopedHandle{
        dispatcher,
        activate_type,
        dispatcher.append_listener(activate_type, [this](const event::Event& e) { on_tile_activate(e); })
    };
    // ConnectionHandler cancels the redirect at normal priority and sends its
    // own copy, so read it before that.
    redirect_handle_ = event::ScopedHandle{
        dispatcher,
        redirect_type,
        dispatcher.append_listener(redirect_type, [this](const event::Event& e) { on_send_to_server(e); }, event::Priority::FairlyHigh)
    };
    join_handle_ = event::ScopedHandle{
        dispatcher,
        join_type,
        dispatcher.append_listener(join_type, [this](const event::Event& e) { on_join_request(e); })
    };
    spawn_handle_ = event::ScopedHandle{
        dispatcher,
        spawn_type,
        dispatcher.append_listener(spawn_type, [this](const event::Event& e) { on_spawn(e); })
    };
}

void DoorIdHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().door_id = enabled;
    std::ignore = config_.save();
}

void DoorIdHandler::on_tile_activate(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::TileActivateRequest>*>(&event) };
    if (!evt || evt->direction != event::Direction::ServerBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::TileActivateRequest>() };
    if (!pkt) {
        return;
    }

    const auto& map{ world::World::instance().get_tile_map() };
    const auto& size{ map.get_size() };
    if (pkt->int_x < 0 || pkt->int_y < 0 || pkt->int_x >= size.x || pkt->int_y >= size.y) {
        return;
    }

    const auto index{ static_cast<std::size_t>(pkt->int_x) + static_cast<std::size_t>(pkt->int_y) * static_cast<std::size_t>(size.x) };
    if (index >= map.get_tiles().size()) {
        return;
    }

    const auto& tile{ map.get_tiles()[index] };
    const auto* item{ item::ItemDatabase::instance().get_item(tile.foreground) };
    if (!item || !is_door(item->item_type)) {
        return;
    }

    const auto* door{ tile.extra.get_as<world::tile_extra::Door>() };
    used_door_ = UsedDoor{ door ? door->label : std::string{}, std::chrono::steady_clock::now() };
}

void DoorIdHandler::on_send_to_server(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::OnSendToServer>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::OnSendToServer>() };
    if (!pkt) {
        return;
    }

    const bool from_door{ used_door_ && std::chrono::steady_clock::now() - used_door_->when < DOOR_WINDOW };
    const auto label{ from_door ? used_door_->label : std::string{} };
    used_door_.reset();

    // "0" on every ordinary world join.
    if (const auto id{ trim(pkt->door_id) }; !id.empty() && id != "0" && id != "-1") {
        pending_message_ = label.empty()
            ? fmt::format("`9[Door ID]`` `2{}``", id)
            : fmt::format("`9[Door ID]`` `2{}`` `o(door `w{}``)``", id, label);
    }
    else if (from_door) {
        pending_message_ = "`9[Door ID]`` `4No Door ID found``";
    }
}

void DoorIdHandler::on_join_request(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::JoinRequest>*>(&event) };
    if (!evt || evt->direction != event::Direction::ServerBound) {
        return;
    }

    // Joining from the world menu: whatever door came before is not this trip.
    used_door_.reset();
    pending_message_.reset();
    if (!is_enabled()) {
        return;
    }

    // "name|WORLD|DOORID" when entering at a door (bots, /warp WORLD|ID).
    if (const auto pkt{ evt->get<packet::message::JoinRequest>() }) {
        if (const auto id{ trim(pkt->text_parse.get("name", 1)) }; !id.empty()) {
            pending_message_ = fmt::format("`9[Door ID]`` `w{}`` `2{}``", pkt->world_name, id);
        }
    }
}

void DoorIdHandler::on_spawn(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::OnSpawn>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound || !pending_message_) {
        return;
    }

    if (const auto pkt{ evt->get<packet::game::OnSpawn>() }; !pkt || pkt->type != "local") {
        return;
    }

    if (is_enabled()) {
        const auto& world_name{ world::World::instance().get_name() };
        send_log(world_name.empty() ? *pending_message_ : fmt::format("{} `oin `w{}``", *pending_message_, world_name));
    }
    pending_message_.reset();
}

void DoorIdHandler::send_log(const std::string& msg) const
{
    packet::message::Log log{};
    log.msg = msg;
    std::ignore = packet::PacketHelper::write(log, server_);
}
}
