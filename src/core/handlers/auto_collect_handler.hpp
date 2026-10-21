#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../config.hpp"
#include "../scheduler.hpp"
#include "../../event/event.hpp"
#include "../../network/client.hpp"

namespace core::handlers {
// Picks up dropped objects near the local player by sending the same
// ITEM_ACTIVATE_OBJECT_REQUEST the client sends when it walks over a drop.
// Runs whenever the player moves or something is dropped, always on the main
// thread. With a delay set, requests are spaced out and a timer picks up what
// is still waiting even while the player stands still.
class AutoCollectHandler {
public:
    static constexpr float DEFAULT_RANGE{ 2.0f };
    static constexpr float MIN_RANGE{ 1.0f };
    static constexpr float MAX_RANGE{ 5.0f };
    static constexpr int MIN_DELAY_MS{ 0 };
    static constexpr int MAX_DELAY_MS{ 5000 };

    AutoCollectHandler(event::Dispatcher& dispatcher, network::Client& client, std::shared_ptr<Scheduler> scheduler, Config& config);

    AutoCollectHandler(const AutoCollectHandler&) = delete;
    AutoCollectHandler& operator=(const AutoCollectHandler&) = delete;
    AutoCollectHandler(AutoCollectHandler&&) noexcept = delete;
    AutoCollectHandler& operator=(AutoCollectHandler&&) noexcept = delete;
    ~AutoCollectHandler() = default;

    [[nodiscard]] bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled);

    [[nodiscard]] float get_range() const { return range_; }
    // Clamps to [MIN_RANGE, MAX_RANGE] and returns the range actually used.
    float set_range(float range_tiles);

    // Minimum time between two pickup requests; 0 sends them right away.
    [[nodiscard]] int get_delay_ms() const { return delay_ms_; }
    // Clamps to [MIN_DELAY_MS, MAX_DELAY_MS] and returns the delay actually used.
    int set_delay_ms(int delay_ms);

    // Sends pickup requests for drops in range. Returns how many were sent.
    std::size_t collect_nearby();

    // Drops of `item_id` that appear within `window` are never picked up, so
    // items the player drops on purpose are not collected straight back.
    void ignore_drops_of(std::uint16_t item_id, std::chrono::milliseconds window);

private:
    // Requests that get no answer are retried a few times, then given up on so
    // an object the server refuses to hand over is not spammed forever.
    static constexpr auto RETRY_DELAY{ std::chrono::milliseconds{ 1000 } };
    static constexpr int MAX_ATTEMPTS{ 3 };
    static constexpr std::size_t MAX_REQUESTS_PER_PASS{ 4 };

    struct Attempt {
        int count{ 0 };
        std::chrono::steady_clock::time_point last_sent{};
    };

    void setup_handlers();
    template <packet::PacketId Id, typename Func>
    void listen(event::Direction direction, Func&& func);
    void track_new_drop(std::uint16_t item_id);
    void reset();
    // Runs collect_nearby() again after `wait`, once (later calls are merged).
    void schedule_pass(std::chrono::milliseconds wait);

    event::Dispatcher& dispatcher_;
    network::Client& client_;
    std::shared_ptr<Scheduler> scheduler_;
    Config& config_;

    bool enabled_{ false };
    float range_{ DEFAULT_RANGE };
    int delay_ms_{ 0 };
    std::chrono::steady_clock::time_point last_request_{};
    bool pass_scheduled_{ false };
    std::unordered_map<std::uint32_t, Attempt> attempts_;
    std::unordered_map<std::uint16_t, std::chrono::steady_clock::time_point> ignore_until_;
    std::unordered_set<std::uint32_t> ignored_objects_;

    std::vector<event::ScopedHandle> handles_;
};
}
