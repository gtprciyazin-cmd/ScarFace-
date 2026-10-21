#pragma once
#include <cstddef>
#include <optional>
#include <vector>

#include "../config.hpp"
#include "../../event/event.hpp"
#include "../../network/server.hpp"
#include "../../world/tile.hpp"

namespace core::handlers {
// Changes how this client sees some tiles, by rewriting their bytes in the
// world data and tile updates before the game gets them (the proxy's own map
// keeps the real tiles):
// - entrances/gates are painted green when open to the public, red when not;
// - lava becomes Lava Rock, a plain solid block, so touching it does nothing.
class TileRewriteHandler {
public:
    static constexpr std::uint16_t LAVA_ROCK{ 380 };

    TileRewriteHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config);

    TileRewriteHandler(const TileRewriteHandler&) = delete;
    TileRewriteHandler& operator=(const TileRewriteHandler&) = delete;
    TileRewriteHandler(TileRewriteHandler&&) noexcept = delete;
    TileRewriteHandler& operator=(TileRewriteHandler&&) noexcept = delete;
    ~TileRewriteHandler() = default;

    [[nodiscard]] bool gates_enabled() const { return config_.get_feature_config().public_gates; }
    void set_gates_enabled(bool enabled);

    [[nodiscard]] bool lava_enabled() const { return config_.get_feature_config().anti_lava; }
    void set_lava_enabled(bool enabled);

    // Public or not for an entrance tile; nullopt for anything else.
    [[nodiscard]] static std::optional<bool> is_public_entrance(const world::Tile& tile);

private:
    // Applies every enabled rewrite to the tile record at `offset` of `data`.
    bool patch(std::vector<std::byte>& data, std::size_t offset, const world::Tile& tile) const;
    void on_map_data(const event::Event& event);
    void on_tile_update(const event::Event& event);
    void on_tile_change(const event::Event& event);
    void tell_to_rejoin() const;

    network::Server& server_;
    Config& config_;

    event::ScopedHandle map_handle_;
    event::ScopedHandle update_handle_;
    event::ScopedHandle change_handle_;
};
}
