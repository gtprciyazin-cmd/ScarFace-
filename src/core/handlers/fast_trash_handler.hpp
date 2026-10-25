#pragma once
#include "../config.hpp"
#include "../legit_manager.hpp"
#include "../../event/event.hpp"
#include "../../network/client.hpp"
#include "../../network/server.hpp"

namespace core::handlers {
// Answers the server's "How many to destroy?" dialog after the player presses
// TRASH or RECYCLE: the whole stack goes at once and the dialog never shows.
// Locks are never trashed this way; their dialog shows as usual.
class FastTrashHandler {
public:
    FastTrashHandler(event::Dispatcher& dispatcher, network::Client& client, network::Server& server, LegitManager& legit, Config& config);

    FastTrashHandler(const FastTrashHandler&) = delete;
    FastTrashHandler& operator=(const FastTrashHandler&) = delete;
    FastTrashHandler(FastTrashHandler&&) noexcept = delete;
    FastTrashHandler& operator=(FastTrashHandler&&) noexcept = delete;
    ~FastTrashHandler() = default;

    [[nodiscard]] bool is_enabled() const { return config_.get_feature_config().fast_trash; }
    void set_enabled(bool enabled);

private:
    void on_dialog(const event::Event& event);

    network::Client& client_;
    network::Server& server_;
    LegitManager& legit_;
    Config& config_;

    event::ScopedHandle handle_;
};
}
