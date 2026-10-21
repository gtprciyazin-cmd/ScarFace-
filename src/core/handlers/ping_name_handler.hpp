#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "../config.hpp"
#include "../scheduler.hpp"
#include "../../event/event.hpp"
#include "../../network/client.hpp"
#include "../../network/server.hpp"

namespace core::handlers {
// Shows the ping to the game server after the player's own name, e.g.
// "Name (45 ms)", and keeps it current. Only this client sees it: the name is
// changed with OnNameChanged sent to the game, never to the server.
class PingNameHandler {
public:
    PingNameHandler(event::Dispatcher& dispatcher, std::shared_ptr<Scheduler> scheduler, network::Client& client, network::Server& server, Config& config);

    PingNameHandler(const PingNameHandler&) = delete;
    PingNameHandler& operator=(const PingNameHandler&) = delete;
    PingNameHandler(PingNameHandler&&) noexcept = delete;
    PingNameHandler& operator=(PingNameHandler&&) noexcept = delete;
    ~PingNameHandler();

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().ping_in_name; }
    void set_enabled(bool enabled);

    // Round trip time to the game server right now, in milliseconds.
    [[nodiscard]] std::uint32_t get_ping() const { return client_.round_trip_time(); }

    // The name with the ping appended, coloured green, yellow or red.
    [[nodiscard]] static std::string format_name(std::string_view name, std::uint32_t ping_ms);

    // Renames the local player (/nick). Kept as the base name, so the ping is
    // shown after it instead of the refresh putting the old name back.
    // Returns false when the player is not in a world.
    bool rename(const std::string& name);

private:
    static constexpr auto REFRESH_INTERVAL{ std::chrono::seconds{ 2 } };
    // OnSpawn is forwarded after the listeners run, so the avatar only exists
    // once it has been processed; rename it a moment later.
    static constexpr auto SPAWN_DELAY{ std::chrono::milliseconds{ 400 } };
    static constexpr const char* TASK_TAG{ "ping_name" };

    void on_spawn(const event::Event& event);
    void on_name_changed(const event::Event& event);
    void on_country_state(const event::Event& event);
    // Sends the flag and badges again after renaming a legend (see .cpp).
    void restore_badges(const std::string& shown);

    void start();
    void stop();
    // Sends the name with the current ping when it differs from what is shown.
    void refresh();
    void restore_name();
    // The base name with the current ping, or without it while unknown.
    [[nodiscard]] std::string display_name() const;
    void send_name(const std::string& name);

    event::Dispatcher& dispatcher_;
    std::shared_ptr<Scheduler> scheduler_;
    network::Client& client_;
    network::Server& server_;
    Config& config_;

    // The local player's name as the server last set it, and what is shown now.
    std::int32_t net_id_{ -1 };
    std::string base_name_;
    // Title graphics (titleIcon JSON). OnNameChanged always sets the title
    // too, so it must be sent back or the title disappears.
    std::string base_title_;
    // Flag and badges ("us|showGuild|maxLevel"), from OnSpawn or OnCountryState.
    std::string base_country_;
    std::string shown_name_;

    TaskId task_id_{ INVALID_TASK_ID };
    event::ScopedHandle spawn_handle_;
    event::ScopedHandle name_handle_;
    event::ScopedHandle country_handle_;
};
}
