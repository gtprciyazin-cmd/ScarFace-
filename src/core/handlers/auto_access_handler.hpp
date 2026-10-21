#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>

#include "dialog_interceptor.hpp"
#include "../config.hpp"
#include "../../event/event.hpp"
#include "../../network/client.hpp"
#include "../../network/server.hpp"
#include "../../utils/game_text.hpp"

namespace core::handlers {
// Accepts lock access the moment someone offers it ("X wants to add you to a
// World Lock. Wrench yourself to accept."): wrenches the player, presses
// "Accept Access" and confirms, like a player would, with the dialogs hidden.
class AutoAccessHandler {
public:
    AutoAccessHandler(
        event::Dispatcher& dispatcher,
        network::Client& client,
        network::Server& server,
        DialogInterceptor& dialogs,
        Config& config
    );

    AutoAccessHandler(const AutoAccessHandler&) = delete;
    AutoAccessHandler& operator=(const AutoAccessHandler&) = delete;
    AutoAccessHandler(AutoAccessHandler&&) noexcept = delete;
    AutoAccessHandler& operator=(AutoAccessHandler&&) noexcept = delete;
    ~AutoAccessHandler() = default;

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().auto_access; }
    void set_enabled(bool enabled);


private:
    static constexpr auto DIALOG_TIMEOUT{ std::chrono::milliseconds{ 3000 } };
    // The same offer arrives as a console line and as a talk bubble.
    static constexpr auto DUPLICATE_WINDOW{ std::chrono::seconds{ 3 } };

    void on_console_message(const event::Event& event);
    void accept(const utils::game_text::AccessOffer& offer);
    bool send_text(std::string_view text) const;
    void send_log(const std::string& msg) const;

    network::Client& client_;
    network::Server& server_;
    DialogInterceptor& dialogs_;
    Config& config_;

    std::string last_owner_;
    std::chrono::steady_clock::time_point last_offer_{};
    std::mt19937 random_{ std::random_device{}() };

    event::ScopedHandle handle_;
};
}
