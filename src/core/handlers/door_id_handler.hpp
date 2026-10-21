#pragma once
#include <chrono>
#include <optional>
#include <string>

#include "../config.hpp"
#include "../../event/event.hpp"
#include "../../network/server.hpp"

namespace core::handlers {
// Prints the door ID in the console when a door takes the player to another
// world. The server only reveals it in the sub-server redirect
// (OnSendToServer "ip|doorID|uuid"), so this watches that and says it once the
// player arrives. Read only; nothing is changed or sent to the server.
class DoorIdHandler {
public:
    DoorIdHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config);

    DoorIdHandler(const DoorIdHandler&) = delete;
    DoorIdHandler& operator=(const DoorIdHandler&) = delete;
    DoorIdHandler(DoorIdHandler&&) noexcept = delete;
    DoorIdHandler& operator=(DoorIdHandler&&) noexcept = delete;
    ~DoorIdHandler() = default;

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().door_id; }
    void set_enabled(bool enabled);

private:
    // A door use and the redirect it causes belong together only this long.
    static constexpr auto DOOR_WINDOW{ std::chrono::seconds{ 10 } };

    struct UsedDoor {
        std::string label;
        std::chrono::steady_clock::time_point when;
    };

    void on_tile_activate(const event::Event& event);
    void on_send_to_server(const event::Event& event);
    void on_join_request(const event::Event& event);
    void on_spawn(const event::Event& event);
    void send_log(const std::string& msg) const;

    network::Server& server_;
    Config& config_;

    std::optional<UsedDoor> used_door_;
    // What to say once the player is in the new world.
    std::optional<std::string> pending_message_;

    event::ScopedHandle activate_handle_;
    event::ScopedHandle redirect_handle_;
    event::ScopedHandle join_handle_;
    event::ScopedHandle spawn_handle_;
};
}
