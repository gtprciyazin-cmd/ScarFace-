#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "world_manage_handler.hpp"
#include "../config.hpp"
#include "../../event/event.hpp"
#include "../../network/server.hpp"
#include "../../utils/text_parse.hpp"

namespace core::handlers {
// Bans a player from your world when they throw a listed consumable (snowball,
// egg, war balloon...) at you. The throw shows up as an item flying from them
// to you; the ban goes through their wrench menu, like doing it by hand, so it
// only works in worlds where you may ban. Staff are never touched.
class ConsumableAutobanHandler {
public:
    ConsumableAutobanHandler(event::Dispatcher& dispatcher, network::Server& server, WorldManageHandler& world_manage, Config& config);

    ConsumableAutobanHandler(const ConsumableAutobanHandler&) = delete;
    ConsumableAutobanHandler& operator=(const ConsumableAutobanHandler&) = delete;
    ConsumableAutobanHandler(ConsumableAutobanHandler&&) noexcept = delete;
    ConsumableAutobanHandler& operator=(ConsumableAutobanHandler&&) noexcept = delete;
    ~ConsumableAutobanHandler() = default;

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().consumable_autoban; }
    void set_enabled(bool enabled);

    // The "Autoban Consumables List" settings menu.
    [[nodiscard]] std::string settings_body() const;
    void apply_settings(const utils::TextParse& fields);

private:
    // One ban per player in this window, however much they throw.
    static constexpr auto COOLDOWN{ std::chrono::seconds{ 10 } };
    // A finished trade hands the items over with the same flying animation as
    // a throw, so nothing counts this long after the trade window was in use.
    static constexpr auto TRADE_WINDOW{ std::chrono::seconds{ 10 } };
    // The menu always offers these, ticked or not.
    static constexpr int DEFAULT_LIST[]{ 1368, 274, 368, 614, 874, 962, 3064, 4666, 3400, 1988, 764, 618, 4844, 4846, 4848, 3722, 6080 };

    void on_item_effect(const event::Event& event);
    template <packet::PacketId Id>
    void listen_trade(event::Dispatcher& dispatcher);
    [[nodiscard]] bool is_listed(int item_id) const;
    void send_log(const std::string& msg) const;

    network::Server& server_;
    WorldManageHandler& world_manage_;
    Config& config_;

    std::unordered_map<std::int32_t, std::chrono::steady_clock::time_point> last_ban_;
    std::chrono::steady_clock::time_point last_trade_{};

    event::ScopedHandle handle_;
    std::vector<event::ScopedHandle> trade_handles_;
};
}
