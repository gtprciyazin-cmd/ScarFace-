#include "gem_drop_handler.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <string>
#include <tuple>
#include <vector>

#include <fmt/format.h>

#include "../../packet/game/world.hpp"
#include "../../packet/generic_packets.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../player/player.hpp"
#include "../../world/object.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    constexpr auto map_data_type{ event::packet_event_type(packet::PacketId::SendMapData) };
    constexpr auto object_change_type{ event::packet_event_type(packet::PacketId::ItemChangeObject) };
}

GemDropHandler::GemDropHandler(event::Dispatcher& dispatcher, std::shared_ptr<Scheduler> scheduler, network::Server& server, Config& config)
    : scheduler_{ std::move(scheduler) }
    , server_{ server }
    , config_{ config }
{
    // Lowest: WorldHandler has updated the object map (and its drop ids) by then.
    map_handle_ = event::ScopedHandle{
        dispatcher,
        map_data_type,
        dispatcher.append_listener(map_data_type, [this](const event::Event& e) { on_map_data(e); }, event::Priority::Lowest)
    };
    object_handle_ = event::ScopedHandle{
        dispatcher,
        object_change_type,
        dispatcher.append_listener(object_change_type, [this](const event::Event& e) { on_object_change(e); }, event::Priority::Lowest)
    };
}

void GemDropHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().gem_count = enabled;
    std::ignore = config_.save();
}

void GemDropHandler::on_map_data(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::SendMapData>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    piles_.clear();
    changed_.clear();

    auto& world{ world::World::instance() };
    if (!world.objects_known()) {
        return;
    }

    for (const auto& object : world.get_object_map().get_objects()) {
        if (object.item_id == GEMS) {
            const auto [x, y]{ world::drop_tile(object.pos.x, object.pos.y) };
            piles_[object.object_id] = Pile{ x, y, object.amount };
        }
    }
}

void GemDropHandler::on_object_change(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::ItemChangeObject>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::ItemChangeObject>() };
    auto& world{ world::World::instance() };
    if (!pkt || !world.objects_known()) {
        return; // Without the world's drop ids, piles cannot be told apart.
    }

    const auto [x, y]{ world::drop_tile(pkt->pos_x, pkt->pos_y) };

    if (pkt->object_change_type == -1) {
        if (pkt->item_id != GEMS) {
            return;
        }
        piles_[world.get_object_map().get_drop_id()] = Pile{ x, y, pkt->amount };
        changed_.emplace(x, y);
    }
    else if (pkt->object_change_type == -3) {
        // An existing object merged, split or moved: the server re-sends gem
        // piles this way when it tidies up a tile.
        const auto uid{ static_cast<std::uint32_t>(pkt->item_net_id) };
        if (pkt->item_id != GEMS) {
            piles_.erase(uid);
            return;
        }
        const auto before{ piles_.contains(uid) ? piles_[uid].amount : 0 };
        piles_[uid] = Pile{ x, y, pkt->amount };
        if (pkt->amount > before) {
            changed_.emplace(x, y);
        }
    }
    else {
        // Removed (-2) or picked up by someone: the object id is in item_id.
        if (!piles_.erase(static_cast<std::uint32_t>(pkt->item_id))) {
            piles_.erase(static_cast<std::uint32_t>(pkt->item_net_id));
        }
        return;
    }

    if (!is_enabled() || flush_scheduled_) {
        return;
    }

    flush_scheduled_ = true;
    Scheduler* const scheduler{ scheduler_.get() };
    scheduler_->schedule_delayed(
        [this, scheduler] { scheduler->post_to_main_thread([this] { flush(); }); },
        GATHER_DELAY,
        "gem_drop"
    );
}

int GemDropHandler::total_at(const int x, const int y) const
{
    int total{ 0 };
    for (const auto& pile : piles_ | std::views::values) {
        if (pile.x == x && pile.y == y) {
            total += pile.amount;
        }
    }
    return total;
}

void GemDropHandler::flush()
{
    flush_scheduled_ = false;
    const auto changed{ std::exchange(changed_, {}) };
    if (!is_enabled() || changed.empty()) {
        return;
    }

    // Only one such box exists; show the tile with the most gems.
    int best_x{ 0 };
    int best_y{ 0 };
    int best_total{ 0 };
    for (const auto& [x, y] : changed) {
        if (const auto total{ total_at(x, y) }; total > best_total) {
            best_x = x;
            best_y = y;
            best_total = total;
        }
    }
    if (best_total <= 0) {
        return;
    }

    const auto others{ changed.size() - 1 };
    show_on_tile(best_x, best_y, others == 0
        ? fmt::format("`2{}`` gems", best_total)
        : fmt::format("`2{}`` gems `o(+{} tiles)``", best_total, others));
}

void GemDropHandler::show_summary()
{
    struct Entry {
        int x;
        int y;
        int total;
        int distance;
    };

    auto& world{ world::World::instance() };
    glm::ivec2 me{ 0, 0 };
    if (const auto local{ world.get_local_player().lock() }) {
        me = local->position() / 32;
    }

    std::vector<Entry> entries{};
    std::set<std::pair<int, int>> seen{};
    for (const auto& pile : piles_ | std::views::values) {
        if (seen.emplace(pile.x, pile.y).second) {
            entries.push_back({ pile.x, pile.y, total_at(pile.x, pile.y), std::abs(pile.x - me.x) + std::abs(pile.y - me.y) });
        }
    }

    packet::message::Log log{};
    if (entries.empty()) {
        log.msg = "`9[Gems]`` No gems on the ground here.";
        std::ignore = packet::PacketHelper::write(log, server_);
        return;
    }

    std::ranges::sort(entries, {}, &Entry::distance);
    int sum{ 0 };
    for (const auto& entry : entries) {
        sum += entry.total;
    }

    log.msg = fmt::format("`9[Gems]`` `2{}`` gems on `w{}`` tiles:", sum, entries.size());
    std::ignore = packet::PacketHelper::write(log, server_);
    for (std::size_t i{ 0 }; i < std::min<std::size_t>(entries.size(), 8); ++i) {
        log.msg = fmt::format("`9[Gems]`` `2{}`` at `w{},{}``", entries[i].total, entries[i].x, entries[i].y);
        std::ignore = packet::PacketHelper::write(log, server_);
    }

    const auto biggest{ std::ranges::max_element(entries, {}, &Entry::total) };
    show_on_tile(biggest->x, biggest->y, fmt::format("`2{}`` gems", biggest->total));
}

void GemDropHandler::show_on_tile(const int x, const int y, const std::string& text) const
{
    const auto& size{ world::World::instance().get_tile_map().get_size() };
    if (x < 0 || y < 0 || x >= size.x || y >= size.y) {
        return; // The client looks the tile up and would crash on a bad one.
    }

    // A box anchored to the tile, closed again by the client after a delay.
    packet::GenericVariantPacket message{};
    message.variant = packet::PacketVariant{
        std::string{ "OnPersistentNPCMessage" },
        static_cast<std::uint32_t>(x + y * size.x),
        text,
        std::uint32_t{ 1 }
    };
    std::ignore = packet::PacketHelper::write(message, server_);

    packet::GenericVariantPacket clear{};
    clear.variant = packet::PacketVariant{ std::string{ "OnClearNPCMessage" } };
    clear.game_packet.int_data = static_cast<std::int32_t>(SHOW_FOR.count());
    std::ignore = packet::PacketHelper::write(clear, server_);
}
}
