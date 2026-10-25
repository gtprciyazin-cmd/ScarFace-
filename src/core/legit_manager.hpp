#pragma once
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <random>
#include <string>

#include "config.hpp"
#include "scheduler.hpp"
#include "../utils/text_parse.hpp"

namespace core {
// Legit mode: spaces out the packets the proxy sends on the player's behalf
// (lock drops, auto collect pickups, dialog answers, pull/kick/ban) like a
// person clicking. Each action is followed by a random 80-250 ms gap, and
// every third action in a row by a longer 300-600 ms break.
//
// It never sleeps. Packets are forwarded on the main thread, so sleeping there
// would freeze the game; instead actions wait in a queue and the scheduler
// runs them on the main thread when their turn comes. The game's own packets
// (movement, punches, chat) are never delayed. Main thread only.
class LegitManager {
public:
    static constexpr int MIN_GAP_MS{ 0 };
    static constexpr int MAX_GAP_MS{ 2000 };

    LegitManager(std::shared_ptr<Scheduler> scheduler, Config& config);

    LegitManager(const LegitManager&) = delete;
    LegitManager& operator=(const LegitManager&) = delete;
    LegitManager(LegitManager&&) noexcept = delete;
    LegitManager& operator=(LegitManager&&) noexcept = delete;
    ~LegitManager() = default;

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().legit_mode; }
    void set_enabled(bool enabled);

    // Runs `action` now if its turn has come, otherwise queues it behind the
    // others. With legit mode off it always runs now.
    void run(std::function<void()> action);

    // For loops that come back on their own (auto collect): takes the turn if
    // it is free and nothing is queued. False means try again in ready_in().
    [[nodiscard]] bool try_acquire();
    // How long until the next action may go; zero when it may go now.
    [[nodiscard]] std::chrono::milliseconds ready_in() const;

    [[nodiscard]] int get_min_delay_ms() const { return config_.get_feature_config().legit_min_delay_ms; }
    [[nodiscard]] int get_max_delay_ms() const { return config_.get_feature_config().legit_max_delay_ms; }
    // Clamps both to [MIN_GAP_MS, MAX_GAP_MS], swapping them if reversed.
    void set_delay_range(int min_ms, int max_ms);

    // The "Legit Mode" settings menu.
    [[nodiscard]] std::string settings_body() const;
    void apply_settings(const utils::TextParse& fields);

private:
    // This many actions in a row, then a longer break.
    static constexpr int BURST_LIMIT{ 3 };
    static constexpr int BREAK_MIN_MS{ 300 };
    static constexpr int BREAK_MAX_MS{ 600 };
    // A pause this long ends a burst.
    static constexpr auto IDLE_RESET{ std::chrono::seconds{ 2 } };

    // Runs every queued action whose turn has come; schedules itself for the rest.
    void pump();
    // Picks the gap before the next action.
    void mark_action();
    [[nodiscard]] int random_int(int min, int max);

    std::shared_ptr<Scheduler> scheduler_;
    Config& config_;

    std::mt19937 rng_;
    std::deque<std::function<void()>> queue_;
    bool pump_scheduled_{ false };
    int burst_{ 0 };
    std::chrono::steady_clock::time_point last_action_{};
    std::chrono::steady_clock::time_point next_allowed_{};
};
}
