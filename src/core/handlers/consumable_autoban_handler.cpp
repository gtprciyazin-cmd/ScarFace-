#include "consumable_autoban_handler.hpp"

#include <algorithm>
#include <charconv>
#include <ranges>
#include <set>
#include <tuple>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../../item/item_database.hpp"
#include "../../packet/game/message.hpp"
#include "../../packet/game/world.hpp"
#include "../../packet/message/dialog_return.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../player/player.hpp"
#include "../../utils/color_codes.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    constexpr auto item_effect_type{ event::packet_event_type(packet::PacketId::ItemEffect) };

    [[nodiscard]] std::string item_name(const int item_id)
    {
        const auto* item{ item::ItemDatabase::instance().get_item(static_cast<std::uint32_t>(item_id)) };
        return item ? item->item_name : fmt::format("Item {}", item_id);
    }
}

ConsumableAutobanHandler::ConsumableAutobanHandler(event::Dispatcher& dispatcher, network::Server& server, WorldManageHandler& world_manage, Config& config)
    : server_{ server }
    , world_manage_{ world_manage }
    , config_{ config }
{
    handle_ = event::ScopedHandle{
        dispatcher,
        item_effect_type,
        dispatcher.append_listener(item_effect_type, [this](const event::Event& e) { on_item_effect(e); })
    };
    listen_trade<packet::PacketId::OnStartTrade>(dispatcher);
    listen_trade<packet::PacketId::OnTradeStatus>(dispatcher);
    listen_trade<packet::PacketId::OnForceTradeEnd>(dispatcher);
    // Accepting the "are you sure" box is the last step before the items move.
    listen_trade<packet::PacketId::DialogReturn>(dispatcher);
}

template <packet::PacketId Id>
void ConsumableAutobanHandler::listen_trade(event::Dispatcher& dispatcher)
{
    constexpr auto type{ event::packet_event_type(Id) };
    trade_handles_.emplace_back(
        dispatcher,
        type,
        dispatcher.append_listener(type, [this](const event::Event& event) {
            const auto* evt{ dynamic_cast<const event::TypedPacketEvent<Id>*>(&event) };
            if (!evt) {
                return;
            }
            if constexpr (Id == packet::PacketId::DialogReturn) {
                const auto pkt{ evt->template get<packet::message::DialogReturn>() };
                if (evt->direction != event::Direction::ServerBound || !pkt || !pkt->dialog_name.starts_with("trade")) {
                    return;
                }
            }
            else if (evt->direction != event::Direction::ClientBound) {
                return;
            }
            last_trade_ = std::chrono::steady_clock::now();
        })
    );
}

void ConsumableAutobanHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().consumable_autoban = enabled;
    std::ignore = config_.save();
}

bool ConsumableAutobanHandler::is_listed(const int item_id) const
{
    return std::ranges::contains(config_.get_feature_config().autoban_consumables, item_id);
}

void ConsumableAutobanHandler::on_item_effect(const event::Event& event)
{
    if (!is_enabled()) {
        return;
    }

    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::ItemEffect>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto now{ std::chrono::steady_clock::now() };
    if (now - last_trade_ < TRADE_WINDOW) {
        return;
    }

    const auto pkt{ evt->get<packet::game::ItemEffect>() };
    auto& world{ world::World::instance() };
    const auto me{ world.get_local_net_id() };
    // Only things thrown at you, and only listed ones: the same animation also
    // carries friendly gifts.
    if (!pkt || pkt->mode != packet::game::ItemEffect::PLAYER_TO_PLAYER || me < 0
        || pkt->target_net_id != me || pkt->thrower_net_id == me || !is_listed(pkt->item_id)) {
        return;
    }

    const auto thrower{ world.get_player(pkt->thrower_net_id).lock() };
    if (!thrower) {
        return;
    }

    const auto name{ utils::strip_color_codes(thrower->name()) };
    if (thrower->mod_state() != 0 || thrower->supermod_state() != 0 || name.starts_with('@')) {
        return; // Never staff.
    }

    if (const auto last{ last_ban_.find(pkt->thrower_net_id) }; last != last_ban_.end() && now - last->second < COOLDOWN) {
        return;
    }
    last_ban_[pkt->thrower_net_id] = now;

    spdlog::info("Autoban: {} threw item {} at us", name, pkt->item_id);
    if (world_manage_.request(WorldManageHandler::Action::Ban, pkt->thrower_net_id, name) == WorldManageHandler::Result::Sent) {
        send_log(fmt::format("`4Autoban: ``banning `w{}`` for using `w{}``", name, item_name(pkt->item_id)));
    }
}

std::string ConsumableAutobanHandler::settings_body() const
{
    // Defaults first, then anything the player added.
    std::vector<int> items{ std::begin(DEFAULT_LIST), std::end(DEFAULT_LIST) };
    for (const int id : config_.get_feature_config().autoban_consumables) {
        if (!std::ranges::contains(items, id)) {
            items.push_back(id);
        }
    }

    std::string d{};
    d += fmt::format("add_checkbox|ab_enabled|`2Enable `9Consumables Autobanner``|{}\n", is_enabled() ? "1" : "0");
    d += "add_smalltext|`sBans whoever throws a ticked item at you, through their wrench menu. Works only in worlds where you can ban; staff are never banned.``|left|\n";
    d += "add_spacer|small|\n";
    for (const int id : items) {
        d += fmt::format("add_label_with_icon|small|`w{}``|left|{}|\n", item_name(id), id);
        d += fmt::format("add_checkbox|ab_{}|`oAutoban``|{}\n", id, is_listed(id) ? "1" : "0");
    }
    d += "add_spacer|small|\n";
    d += "add_text_input|ab_add|Add item IDs||40|\n";
    d += "add_smalltext|`sMore item IDs, separated by commas (e.g. 1368,874).``|left|\n";
    return d;
}

void ConsumableAutobanHandler::apply_settings(const utils::TextParse& fields)
{
    auto& features{ config_.get_feature_config() };
    if (const auto enabled{ fields.get<std::string>("ab_enabled") }; !enabled.empty()) {
        features.consumable_autoban = enabled == "1";
    }

    std::set<int> listed{ features.autoban_consumables.begin(), features.autoban_consumables.end() };
    std::vector<int> shown{ std::begin(DEFAULT_LIST), std::end(DEFAULT_LIST) };
    shown.insert(shown.end(), features.autoban_consumables.begin(), features.autoban_consumables.end());
    for (const int id : shown) {
        if (const auto value{ fields.get<std::string>(fmt::format("ab_{}", id)) }; !value.empty()) {
            if (value == "1") {
                listed.insert(id);
            }
            else {
                listed.erase(id);
            }
        }
    }

    // "1368, 874" -> 1368 874; anything that is not an item id is skipped.
    const auto extra{ fields.get<std::string>("ab_add") };
    for (std::size_t start{ 0 }; start < extra.size();) {
        const auto end{ std::min(extra.find(',', start), extra.size()) };
        auto token{ std::string_view{ extra }.substr(start, end - start) };
        while (!token.empty() && token.front() == ' ') {
            token.remove_prefix(1);
        }
        int id{ 0 };
        if (const auto [ptr, ec]{ std::from_chars(token.data(), token.data() + token.size(), id) }; ec == std::errc{} && id > 0) {
            listed.insert(id);
        }
        start = end + 1;
    }

    features.autoban_consumables.assign(listed.begin(), listed.end());
    std::ignore = config_.save();
}

void ConsumableAutobanHandler::send_log(const std::string& msg) const
{
    packet::message::Log log{};
    log.msg = msg;
    std::ignore = packet::PacketHelper::write(log, server_);
}
}
