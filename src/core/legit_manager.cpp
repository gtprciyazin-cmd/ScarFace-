#include "legit_manager.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <tuple>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace core {
namespace {
    [[nodiscard]] std::optional<int> parse_int(const std::string& text)
    {
        int value{ 0 };
        if (const auto [ptr, ec]{ std::from_chars(text.data(), text.data() + text.size(), value) }; ec != std::errc{} || ptr != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    }
}

LegitManager::LegitManager(std::shared_ptr<Scheduler> scheduler, Config& config)
    : scheduler_{ std::move(scheduler) }
    , config_{ config }
    , rng_{ std::random_device{}() }
{
    // Older config files have no legit settings yet; bring them into range.
    const auto& features{ config_.get_feature_config() };
    set_delay_range(features.legit_min_delay_ms, features.legit_max_delay_ms);

    if (is_enabled()) {
        spdlog::info("Legit mode: proxy actions are spaced {}-{} ms apart", get_min_delay_ms(), get_max_delay_ms());
    }
}

void LegitManager::set_enabled(const bool enabled)
{
    config_.get_feature_config().legit_mode = enabled;
    std::ignore = config_.save();

    // Turned off: whatever was waiting goes now.
    if (!enabled) {
        pump();
    }
}

void LegitManager::set_delay_range(int min_ms, int max_ms)
{
    min_ms = std::clamp(min_ms, MIN_GAP_MS, MAX_GAP_MS);
    max_ms = std::clamp(max_ms, MIN_GAP_MS, MAX_GAP_MS);
    if (min_ms > max_ms) {
        std::swap(min_ms, max_ms);
    }

    auto& features{ config_.get_feature_config() };
    if (features.legit_min_delay_ms == min_ms && features.legit_max_delay_ms == max_ms) {
        return;
    }

    features.legit_min_delay_ms = min_ms;
    features.legit_max_delay_ms = max_ms;
    std::ignore = config_.save();
}

void LegitManager::run(std::function<void()> action)
{
    if (!is_enabled()) {
        action();
        return;
    }

    queue_.push_back(std::move(action));
    pump();
}

bool LegitManager::try_acquire()
{
    if (!is_enabled()) {
        return true;
    }

    if (!queue_.empty() || ready_in() > std::chrono::milliseconds::zero()) {
        return false;
    }

    mark_action();
    return true;
}

std::chrono::milliseconds LegitManager::ready_in() const
{
    if (!is_enabled()) {
        return std::chrono::milliseconds::zero();
    }

    const auto now{ std::chrono::steady_clock::now() };
    if (next_allowed_ <= now) {
        return std::chrono::milliseconds::zero();
    }

    return std::chrono::ceil<std::chrono::milliseconds>(next_allowed_ - now);
}

void LegitManager::pump()
{
    while (!queue_.empty()) {
        if (const auto wait{ ready_in() }; wait > std::chrono::milliseconds::zero()) {
            if (pump_scheduled_) {
                return;
            }

            // Scheduler callbacks run on a worker thread; hop back to the main thread.
            pump_scheduled_ = true;
            const auto scheduler{ scheduler_.get() };
            scheduler_->schedule_delayed(
                [this, scheduler] {
                    scheduler->post_to_main_thread([this] {
                        pump_scheduled_ = false;
                        pump();
                    });
                },
                wait,
                "legit"
            );
            return;
        }

        // Taken off the queue first: the action may queue the next one.
        auto action{ std::move(queue_.front()) };
        queue_.pop_front();
        if (is_enabled()) {
            mark_action();
        }
        action();
    }
}

void LegitManager::mark_action()
{
    const auto now{ std::chrono::steady_clock::now() };
    if (now - last_action_ >= IDLE_RESET) {
        burst_ = 0;
    }
    last_action_ = now;

    int gap_ms{ 0 };
    if (++burst_ >= BURST_LIMIT) {
        burst_ = 0;
        gap_ms = random_int(BREAK_MIN_MS, BREAK_MAX_MS);
    }
    else {
        gap_ms = random_int(get_min_delay_ms(), get_max_delay_ms());
    }

    next_allowed_ = now + std::chrono::milliseconds{ gap_ms };
}

int LegitManager::random_int(const int min, const int max)
{
    return std::uniform_int_distribution<int>{ min, max }(rng_);
}

std::string LegitManager::settings_body() const
{
    std::string d{};
    d += fmt::format("add_checkbox|lg_enabled|`2Enable `9Legit Mode``|{}\n", is_enabled() ? "1" : "0");
    d += "add_smalltext|`sSpaces out what the proxy does for you (/wd, /dd, /bd, /dropall, auto collect, fast drop and trash, pull/kick/ban) like a person clicking. Your own movement and punches are never slowed.``|left|\n";
    d += "add_spacer|small|\n";
    d += fmt::format("add_text_input|lg_min|Shortest gap (ms)|{}|5|\n", get_min_delay_ms());
    d += fmt::format("add_text_input|lg_max|Longest gap (ms)|{}|5|\n", get_max_delay_ms());
    d += fmt::format(
        "add_smalltext|`sEach action waits a random time between the two; after {} in a row it takes a {}-{} ms break.``|left|\n",
        BURST_LIMIT,
        BREAK_MIN_MS,
        BREAK_MAX_MS
    );
    return d;
}

void LegitManager::apply_settings(const utils::TextParse& fields)
{
    if (const auto min_ms{ parse_int(fields.get<std::string>("lg_min")) }, max_ms{ parse_int(fields.get<std::string>("lg_max")) }; min_ms && max_ms) {
        set_delay_range(*min_ms, *max_ms);
    }

    if (const auto enabled{ fields.get<std::string>("lg_enabled") }; !enabled.empty()) {
        set_enabled(enabled == "1");
    }
}
}
