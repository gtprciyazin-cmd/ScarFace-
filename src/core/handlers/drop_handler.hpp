#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>
#include <optional>
#include <string>
#include <string_view>

#include "auto_collect_handler.hpp"
#include "dialog_interceptor.hpp"
#include "../legit_manager.hpp"
#include "../../network/client.hpp"
#include "../../network/server.hpp"
#include "../../player/inventory.hpp"

namespace core::handlers {
// Drops items without the "how many?" dialog: sends the same "action|drop"
// the client sends, hides the server's drop_item dialog and answers it right
// away with the requested amount. For locks it first shatters a higher tier
// lock when the player is short (e.g. a Diamond Lock into 100 World Locks).
class DropHandler {
public:
    static constexpr int MAX_AMOUNT{ player::Inventory::MAX_STACK };

    enum class Result {
        Sent,
        Converting,
        InvalidAmount,
        NotConnected,
        NotInWorld,
    };

    DropHandler(
        network::Client& client,
        network::Server& server,
        DialogInterceptor& dialogs,
        AutoCollectHandler& auto_collect,
        LegitManager& legit
    );

    DropHandler(const DropHandler&) = delete;
    DropHandler& operator=(const DropHandler&) = delete;
    DropHandler(DropHandler&&) noexcept = delete;
    DropHandler& operator=(DropHandler&&) noexcept = delete;
    ~DropHandler() = default;

    Result request_drop(std::uint16_t item_id, int amount, const std::string& item_name);

    // Drops every World, Diamond and Blue Gem Lock the player holds, one lock
    // type after another; each takes the real count from its drop dialog.
    Result request_drop_all_locks();

private:
    // How long to wait for the server's drop dialog before giving up.
    static constexpr auto DIALOG_TIMEOUT{ std::chrono::milliseconds{ 3000 } };
    // How long auto collect leaves fresh drops of the dropped item alone.
    static constexpr auto AUTO_COLLECT_IGNORE_WINDOW{ std::chrono::milliseconds{ 5000 } };
    // Time for the server to apply shattered locks before the drop is requested.
    static constexpr auto CONVERSION_DELAY{ std::chrono::milliseconds{ 600 } };
    // Pause between the lock types of a drop-all, so the drops do not race.
    static constexpr auto DROP_ALL_STEP_DELAY{ std::chrono::milliseconds{ 400 } };

    // Shatters `count` x `source_item`, one per turn, then drops.
    void shatter(std::uint16_t source_item, int count, std::uint16_t item_id, int amount, const std::string& item_name);
    void start_drop(std::uint16_t item_id, int amount, const std::string& item_name);
    void drop_all_step(std::size_t index, std::shared_ptr<std::vector<std::string>> dropped);
    bool on_drop_dialog(const packet::game::DialogInfo& dialog, std::uint16_t item_id, int amount, const std::string& item_name);
    bool send_text(std::string_view text) const;
    void send_log(const std::string& msg) const;

    network::Client& client_;
    network::Server& server_;
    DialogInterceptor& dialogs_;
    AutoCollectHandler& auto_collect_;
    LegitManager& legit_;
};
}
