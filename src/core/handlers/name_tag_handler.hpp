#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../config.hpp"
#include "../scheduler.hpp"
#include "../../event/event.hpp"
#include "../../network/client.hpp"
#include "../../network/server.hpp"

namespace core::handlers {
// Owns what this client shows after players' names: the ping after your own
// ("Name (45 ms)") and everyone's last real roulette spin ("Name [27]"). Only
// this client sees it: names are changed with OnNameChanged sent to the game,
// never to the server. One owner for every name, so the two never fight.
class NameTagHandler {
public:
    NameTagHandler(event::Dispatcher& dispatcher, std::shared_ptr<Scheduler> scheduler, network::Client& client, network::Server& server, Config& config);

    NameTagHandler(const NameTagHandler&) = delete;
    NameTagHandler& operator=(const NameTagHandler&) = delete;
    NameTagHandler(NameTagHandler&&) noexcept = delete;
    NameTagHandler& operator=(NameTagHandler&&) noexcept = delete;
    ~NameTagHandler();

    // Ping after your own name.
    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().ping_in_name; }
    void set_enabled(bool enabled);

    // Last roulette spin after every player's name.
    [[nodiscard]] bool spins_enabled() const { return config_.get_feature_config().last_spin_names; }
    void set_spins_enabled(bool enabled);

    // Round trip time to the game server right now, in milliseconds.
    [[nodiscard]] std::uint32_t get_ping() const { return client_.round_trip_time(); }

    // Renames the local player (/nick). Kept as the base name, so the tags
    // stay after it. Returns false when the player is not in a world.
    bool rename(const std::string& name);

    // A real spin: tag the spinner once the wheel stops.
    void set_last_spin(std::int32_t net_id, int number, const std::string& colour_code, std::chrono::milliseconds delay);

private:
    static constexpr auto REFRESH_INTERVAL{ std::chrono::seconds{ 2 } };
    // OnSpawn is forwarded after the listeners run, so the avatar only exists
    // once it has been processed; rename it a moment later.
    static constexpr auto SPAWN_DELAY{ std::chrono::milliseconds{ 400 } };
    static constexpr const char* TASK_TAG{ "name_tag" };

    struct Player {
        // As the server last set them.
        std::string base_name;
        // Title graphics (titleIcon JSON); OnNameChanged always sets the title
        // too, so it must be sent back or the title disappears.
        std::string title;
        // Flag and badges ("us|showGuild|maxLevel").
        std::string country;
        // What the client shows now.
        std::string shown;
    };

    void on_spawn(const event::Event& event);
    void on_name_changed(const event::Event& event);
    void on_country_state(const event::Event& event);
    void on_remove(const event::Event& event);
    void on_join(const event::Event& event);

    void start_ping_timer();
    void stop_ping_timer();
    void refresh_later(std::int32_t net_id, std::chrono::milliseconds delay);
    void refresh(std::int32_t net_id);
    void refresh_all();
    // The base name with every tag that applies to this player.
    [[nodiscard]] std::string display_name(std::int32_t net_id, const Player& player) const;
    void send_name(std::int32_t net_id, Player& player, const std::string& name);
    // Renaming a legend makes the client reset the flag and wipe every badge;
    // set them again right after.
    void restore_badges(std::int32_t net_id, const Player& player) const;
    [[nodiscard]] bool in_world(std::int32_t net_id) const;

    std::shared_ptr<Scheduler> scheduler_;
    network::Client& client_;
    network::Server& server_;
    Config& config_;

    std::unordered_map<std::int32_t, Player> players_;
    std::int32_t local_net_id_{ -1 };
    // Last spin by name, so a player keeps the tag after re-entering.
    std::unordered_map<std::string, std::string> spins_;

    TaskId ping_task_{ INVALID_TASK_ID };
    std::vector<event::ScopedHandle> handles_;
};
}
