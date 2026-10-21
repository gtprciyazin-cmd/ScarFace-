#pragma once
#include <chrono>
#include <cstdint>

#include "../config.hpp"
#include "../../event/event.hpp"
#include "../../network/server.hpp"

namespace core::handlers {
// Shows what a dice block will land on the moment it is punched: the server
// already sends the face in the damage packet while the client is still
// animating the roll. Only client-bound text; nothing reaches the server.
class DiceHandler {
public:
    DiceHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config);

    DiceHandler(const DiceHandler&) = delete;
    DiceHandler& operator=(const DiceHandler&) = delete;
    DiceHandler(DiceHandler&&) noexcept = delete;
    DiceHandler& operator=(DiceHandler&&) noexcept = delete;
    ~DiceHandler() = default;

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().instant_dice; }
    void set_enabled(bool enabled);

private:
    // Dice Block; other dice items (Roshambo...) are shown as raw values.
    static constexpr std::uint16_t DICE_BLOCK{ 456 };

    void on_damage(const event::Event& event);

    network::Server& server_;
    Config& config_;

    // The server can repeat the damage packet during the roll cooldown.
    std::int64_t last_tile_{ -1 };
    std::uint8_t last_value_{ 0 };
    std::chrono::steady_clock::time_point last_time_{};

    event::ScopedHandle handle_;
};
}
