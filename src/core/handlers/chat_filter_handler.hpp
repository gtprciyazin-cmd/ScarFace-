#pragma once
#include <chrono>
#include <deque>
#include <string>
#include <vector>

#include "../config.hpp"
#include "../../event/event.hpp"
#include "../../network/server.hpp"

namespace core::handlers {
// Hides chat clutter on this client only:
// - private messages ([MSG]) from players, kept aside for /pms; staff
//   messages (@names) always show, and so do your own "Sent to" lines;
// - "You need to be Level N" notices and the tutorial's arrows, hint boxes
//   and popups.
class ChatFilterHandler {
public:
    static constexpr std::size_t MAX_HIDDEN{ 50 };

    ChatFilterHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config);

    ChatFilterHandler(const ChatFilterHandler&) = delete;
    ChatFilterHandler& operator=(const ChatFilterHandler&) = delete;
    ChatFilterHandler(ChatFilterHandler&&) noexcept = delete;
    ChatFilterHandler& operator=(ChatFilterHandler&&) noexcept = delete;
    ~ChatFilterHandler() = default;

    [[nodiscard]] bool hides_private_messages() const { return config_.get_feature_config().hide_private_messages; }
    void set_hide_private_messages(bool enabled);

    [[nodiscard]] bool hides_level_text() const { return config_.get_feature_config().hide_level_text; }
    void set_hide_level_text(bool enabled);

    // Prints the private messages hidden so far, oldest first.
    void show_hidden_messages() const;

private:
    // At most one "hidden" notice for level texts this often.
    static constexpr auto LEVEL_NOTICE_INTERVAL{ std::chrono::seconds{ 30 } };

    template <packet::PacketId Id, typename Packet>
    void listen_text();
    template <packet::PacketId Id>
    void listen_tutorial();
    // True when the line must not reach the game.
    bool filter(const std::string& text);
    void send_log(const std::string& msg) const;

    event::Dispatcher& dispatcher_;
    network::Server& server_;
    Config& config_;

    std::deque<std::string> hidden_;
    std::chrono::steady_clock::time_point last_level_notice_{};

    std::vector<event::ScopedHandle> handles_;
};
}
