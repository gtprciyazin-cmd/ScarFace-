#pragma once
#include <optional>

#include "../config.hpp"
#include "../../event/event.hpp"
#include "../../network/server.hpp"
#include "../../world/tile.hpp"

namespace core::handlers {
// Paints every entrance/gate in the world green when it is open to the public
// and red when it is not, by rewriting the paint bits of those tiles in the
// world data before the game sees it. Only what this client draws changes.
class EntranceColorHandler {
public:
    EntranceColorHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config);

    EntranceColorHandler(const EntranceColorHandler&) = delete;
    EntranceColorHandler& operator=(const EntranceColorHandler&) = delete;
    EntranceColorHandler(EntranceColorHandler&&) noexcept = delete;
    EntranceColorHandler& operator=(EntranceColorHandler&&) noexcept = delete;
    ~EntranceColorHandler() = default;

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().public_gates; }
    void set_enabled(bool enabled);

    // Public or not for an entrance tile; nullopt for anything else.
    [[nodiscard]] static std::optional<bool> is_public_entrance(const world::Tile& tile);

private:
    void on_map_data(const event::Event& event);
    void on_tile_update(const event::Event& event);

    network::Server& server_;
    Config& config_;

    event::ScopedHandle map_handle_;
    event::ScopedHandle update_handle_;
};
}
