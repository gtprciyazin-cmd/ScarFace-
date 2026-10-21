#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "../config.hpp"
#include "../../event/event.hpp"
#include "../../network/client.hpp"
#include "../../network/server.hpp"

namespace core::handlers {
// Teleports the player:
// - Shift + Wrench on any player: next to them, from anywhere in the world
//   (the game sends a wrench for any player on screen);
// - Shift + Click on a tile: there. The game only reports clicks on tiles in
//   reach (and none for the fist on empty air), so this works close by only;
// - /tp <x> <y> or /tp <player>, /pos for your own tile.
// The game is moved with OnSetPos and the server told with one State, the
// packet the game sends when you move.
//
// EXPERIMENTAL: the server may pull you back after a long jump.
class PathfindHandler {
public:
    PathfindHandler(event::Dispatcher& dispatcher, network::Client& client, network::Server& server, Config& config);

    PathfindHandler(const PathfindHandler&) = delete;
    PathfindHandler& operator=(const PathfindHandler&) = delete;
    PathfindHandler(PathfindHandler&&) noexcept = delete;
    PathfindHandler& operator=(PathfindHandler&&) noexcept = delete;
    ~PathfindHandler() = default;

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().pathfind; }
    void set_enabled(bool enabled);

    // False (with a console message) when not in a world or out of bounds.
    bool teleport_to_tile(int tile_x, int tile_y);
    // Teleports next to the player whose name starts with `name`.
    bool teleport_to_player(std::string_view name);
    // "Tile x, y" of the local player in the console.
    void show_position() const;

private:
    static constexpr float TILE_SIZE{ 32.0f };

    void on_tile_change(const event::Event& event);
    void on_wrench(const event::Event& event);
    void teleport(float px, float py);
    void send_log(const std::string& msg) const;

    event::Dispatcher& dispatcher_;
    network::Client& client_;
    network::Server& server_;
    Config& config_;

    event::ScopedHandle handle_;
    event::ScopedHandle wrench_handle_;
};
}
