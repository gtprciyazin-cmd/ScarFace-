#pragma once
#include <cstdint>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>

#include "../config.hpp"
#include "../scheduler.hpp"
#include "../../event/event.hpp"
#include "../../network/server.hpp"

namespace core::handlers {
// Shows how many gems lie on a tile the moment they drop (a break spills
// several piles at once, so changes are gathered for a moment and then the
// tile's total is shown on it). "/gems" lists the gem piles in the world.
// Only client-bound display; nothing reaches the server.
class GemDropHandler {
public:
    static constexpr std::int32_t GEMS{ 112 };

    GemDropHandler(event::Dispatcher& dispatcher, std::shared_ptr<Scheduler> scheduler, network::Server& server, Config& config);

    GemDropHandler(const GemDropHandler&) = delete;
    GemDropHandler& operator=(const GemDropHandler&) = delete;
    GemDropHandler(GemDropHandler&&) noexcept = delete;
    GemDropHandler& operator=(GemDropHandler&&) noexcept = delete;
    ~GemDropHandler() = default;

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().gem_count; }
    void set_enabled(bool enabled);

    // Console list of the gem piles, nearest first; the biggest is shown on its tile.
    void show_summary();


private:
    static constexpr auto GATHER_DELAY{ std::chrono::milliseconds{ 150 } };
    static constexpr auto SHOW_FOR{ std::chrono::milliseconds{ 2500 } };

    struct Pile {
        int x;
        int y;
        int amount;
    };

    void on_map_data(const event::Event& event);
    void on_object_change(const event::Event& event);
    void flush();
    [[nodiscard]] int total_at(int x, int y) const;
    void show_on_tile(int x, int y, const std::string& text) const;

    std::shared_ptr<Scheduler> scheduler_;
    network::Server& server_;
    Config& config_;

    // Gem objects in the world by object id.
    std::unordered_map<std::uint32_t, Pile> piles_;
    // Tiles that got new gems since the last flush.
    std::set<std::pair<int, int>> changed_;
    bool flush_scheduled_{ false };

    event::ScopedHandle map_handle_;
    event::ScopedHandle object_handle_;
};
}
