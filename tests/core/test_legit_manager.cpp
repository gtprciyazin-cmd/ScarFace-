#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include "core/legit_manager.hpp"

using namespace std::chrono_literals;

namespace {
// A fixed gap keeps the timing predictable. Nothing here saves the config.
core::Config& legit_config(const bool enabled, const int gap_ms)
{
    static core::Config config{};
    auto& features{ config.get_feature_config() };
    features.legit_mode = enabled;
    features.legit_min_delay_ms = gap_ms;
    features.legit_max_delay_ms = gap_ms;
    return config;
}
}

TEST(LegitManagerTest, OffRunsEverythingAtOnce)
{
    auto scheduler{ std::make_shared<core::Scheduler>(1) };
    core::LegitManager legit{ scheduler, legit_config(false, 100) };

    int runs{ 0 };
    for (int i{ 0 }; i < 5; ++i) {
        legit.run([&] { ++runs; });
    }

    EXPECT_EQ(runs, 5);
    EXPECT_TRUE(legit.try_acquire());
    EXPECT_EQ(legit.ready_in(), 0ms);
    scheduler->stop();
}

TEST(LegitManagerTest, QueuedActionsRunInOrderAfterTheGap)
{
    auto scheduler{ std::make_shared<core::Scheduler>(1) };
    core::LegitManager legit{ scheduler, legit_config(true, 50) };

    std::vector<int> order{};
    legit.run([&] { order.push_back(1); });
    legit.run([&] { order.push_back(2); });
    legit.run([&] { order.push_back(3); });

    // The first goes at once, the rest wait their turn without blocking.
    ASSERT_EQ(order, std::vector<int>{ 1 });
    EXPECT_GT(legit.ready_in(), 0ms);
    EXPECT_LE(legit.ready_in(), 50ms);
    EXPECT_FALSE(legit.try_acquire());

    const auto start{ std::chrono::steady_clock::now() };
    while (order.size() < 3 && std::chrono::steady_clock::now() - start < 3s) {
        std::this_thread::sleep_for(5ms);
        scheduler->run_main_thread_tasks();
    }

    EXPECT_EQ(order, (std::vector<int>{ 1, 2, 3 }));
    EXPECT_GE(std::chrono::steady_clock::now() - start, 90ms);
    scheduler->stop();
}

TEST(LegitManagerTest, BreakAfterThreeActionsInARow)
{
    auto scheduler{ std::make_shared<core::Scheduler>(1) };
    core::LegitManager legit{ scheduler, legit_config(true, 0) };

    EXPECT_TRUE(legit.try_acquire());
    EXPECT_TRUE(legit.try_acquire());
    EXPECT_TRUE(legit.try_acquire());

    // The third in a row is followed by a 300-600 ms break.
    EXPECT_FALSE(legit.try_acquire());
    EXPECT_GE(legit.ready_in(), 290ms);
    EXPECT_LE(legit.ready_in(), 600ms);
    scheduler->stop();
}
