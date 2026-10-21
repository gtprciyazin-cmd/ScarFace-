#pragma once
#include <memory>

#include "../config.hpp"
#include "../scheduler.hpp"
#include "../../event/event.hpp"
#include "../../network/server.hpp"

namespace core::handlers {
// Zooms the camera out past the game's limit, so the whole world fits on the
// screen. The game's own zoom stops at half the default for players (moderators
// go further); the server's zoom call has no such limit. Only the camera of
// this client changes. Scrolling the mouse wheel afterwards snaps back to the
// game's limit; zoom out again with /zoom.
class ZoomHandler {
public:
    static constexpr float MIN_LEVEL{ 0.05f };
    static constexpr float MAX_LEVEL{ 8.8f };

    ZoomHandler(event::Dispatcher& dispatcher, std::shared_ptr<Scheduler> scheduler, network::Server& server, Config& config);

    ZoomHandler(const ZoomHandler&) = delete;
    ZoomHandler& operator=(const ZoomHandler&) = delete;
    ZoomHandler(ZoomHandler&&) noexcept = delete;
    ZoomHandler& operator=(ZoomHandler&&) noexcept = delete;
    ~ZoomHandler() = default;

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().zoom_out; }
    void set_enabled(bool enabled);

    [[nodiscard]] float get_level() const { return config_.get_feature_config().zoom_level; }
    // Clamps, zooms there and switches the feature on; returns the level used.
    float set_level(float level);

private:
    // The camera is set up when the world loads; zoom after that.
    static constexpr auto JOIN_DELAY{ std::chrono::milliseconds{ 1500 } };
    // 10000 means "the game's default zoom" to the client.
    static constexpr float DEFAULT_ZOOM{ 10000.0f };

    void on_spawn(const event::Event& event);
    void apply() const;
    void zoom_to(float level) const;

    std::shared_ptr<Scheduler> scheduler_;
    network::Server& server_;
    Config& config_;

    event::ScopedHandle handle_;
};
}
