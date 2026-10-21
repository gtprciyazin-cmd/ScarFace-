#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include "../config.hpp"
#include "../scheduler.hpp"
#include "../../event/event.hpp"
#include "../../network/client.hpp"
#include "../../network/server.hpp"

namespace core::handlers {
// Says a message in world chat every few seconds while enabled, exactly as if
// the player typed it. The text and the interval are saved to config.json; the
// spam itself always starts switched off so it never runs right after launch.
class SpamHandler {
public:
    static constexpr int MIN_INTERVAL_S{ 4 };
    static constexpr int MAX_INTERVAL_S{ 3600 };
    static constexpr int DEFAULT_INTERVAL_S{ 10 };
    static constexpr std::size_t MAX_LENGTH{ 120 };

    SpamHandler(event::Dispatcher& dispatcher, std::shared_ptr<Scheduler> scheduler, network::Client& client, network::Server& server, Config& config);

    SpamHandler(const SpamHandler&) = delete;
    SpamHandler& operator=(const SpamHandler&) = delete;
    SpamHandler(SpamHandler&&) noexcept = delete;
    SpamHandler& operator=(SpamHandler&&) noexcept = delete;
    ~SpamHandler();

    [[nodiscard]] bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled);

    [[nodiscard]] const std::string& get_text() const { return config_.get_feature_config().spam_text; }
    // Stores the message without characters that would break the chat packet
    // (newlines, pipes) and cut to MAX_LENGTH. Returns the text actually used.
    const std::string& set_text(std::string_view text);

    [[nodiscard]] int get_interval() const { return config_.get_feature_config().spam_interval_s; }
    // Clamps to [MIN_INTERVAL_S, MAX_INTERVAL_S] and returns the interval used.
    int set_interval(int seconds);

    // What set_text stores for `text`.
    [[nodiscard]] static std::string sanitize(std::string_view text);

private:
    static constexpr const char* TASK_TAG{ "spam" };

    void start();
    void stop();
    void tick();
    void on_server_message(const event::Event& event);
    void on_player_input(const event::Event& event);
    void on_spawn(const event::Event& event);
    void send_log(const std::string& msg) const;

    event::Dispatcher& dispatcher_;
    std::shared_ptr<Scheduler> scheduler_;
    network::Client& client_;
    network::Server& server_;
    Config& config_;

    bool enabled_{ false };
    TaskId task_id_{ INVALID_TASK_ID };
    std::chrono::steady_clock::time_point last_sent_{};

    event::ScopedHandle console_handle_;
    event::ScopedHandle input_handle_;
    event::ScopedHandle spawn_handle_;
};
}
