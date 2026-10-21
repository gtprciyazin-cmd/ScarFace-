#pragma once
#include "../config.hpp"
#include "../../event/event.hpp"
#include "../../network/server.hpp"

namespace core::handlers {
// Other players' punches no longer knock you around. Your own game decides
// the knockback when it sees another player punch the tile you stand on, so
// those punches are shown to it as aimed at no tile: the puncher still swings,
// you just do not fly. Only what this client simulates changes.
class AntipunchHandler {
public:
    AntipunchHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config);

    AntipunchHandler(const AntipunchHandler&) = delete;
    AntipunchHandler& operator=(const AntipunchHandler&) = delete;
    AntipunchHandler(AntipunchHandler&&) noexcept = delete;
    AntipunchHandler& operator=(AntipunchHandler&&) noexcept = delete;
    ~AntipunchHandler() = default;

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().antipunch; }
    void set_enabled(bool enabled);

    // True when a punch at tile (x, y) can reach a player whose box starts at
    // (px, py): within a tile of it, since our position is only as fresh as
    // our last movement packet.
    [[nodiscard]] static bool punch_reaches(int tile_x, int tile_y, float px, float py);

private:
    void on_state(const event::Event& event);

    network::Server& server_;
    Config& config_;

    event::ScopedHandle handle_;
};
}
