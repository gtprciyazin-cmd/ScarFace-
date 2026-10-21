#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "quick_wrench_handler.hpp"
#include "roulette_handler.hpp"
#include "mod_alert_handler.hpp"
#include "fun_facts_handler.hpp"
#include "fast_drop_handler.hpp"
#include "auto_collect_handler.hpp"
#include "hotkey_handler.hpp"
#include "pathfind_handler.hpp"
#include "name_tag_handler.hpp"
#include "spam_handler.hpp"
#include "version_handler.hpp"
#include "../../event/event.hpp"
#include "../../network/server.hpp"
#include "../scheduler.hpp"

namespace core::handlers {
// Backs the in-game settings menus: "/proxy" (every feature on one page, with
// buttons to the detailed menus) and one menu per feature, opened by typing the
// feature's command without arguments. Values are applied when the player
// presses Save and each feature's handler saves them to config.json.
class ProxyMenuHandler {
public:
    ProxyMenuHandler(
        event::Dispatcher& dispatcher,
        network::Server& server,
        std::shared_ptr<Scheduler> scheduler,
        QuickWrenchHandler& quick_wrench,
        RouletteHandler& roulette,
        ModAlertHandler& mod_alert,
        FunFactsHandler& fun_facts,
        FastDropHandler& fast_drop,
        AutoCollectHandler& auto_collect,
        HotkeyHandler& hotkey,
        PathfindHandler& pathfind,
        SpamHandler& spam,
        NameTagHandler& ping_name,
        VersionHandler& version
    );

    ProxyMenuHandler(const ProxyMenuHandler&) = delete;
    ProxyMenuHandler& operator=(const ProxyMenuHandler&) = delete;
    ProxyMenuHandler(ProxyMenuHandler&&) noexcept = delete;
    ProxyMenuHandler& operator=(ProxyMenuHandler&&) noexcept = delete;
    ~ProxyMenuHandler() = default;

    // A switch in the main /proxy menu: a checkbox labelled like
    // "`2Enable `9Fast Drop" with a description line under it and, when the
    // feature has more settings, a button to its own menu.
    struct Toggle {
        std::string id;
        std::string label;
        std::string description;
        std::function<bool()> is_enabled;
        std::function<void(bool)> set_enabled;
        // Button name of the feature's menu (see open_settings), or empty.
        std::string settings{};
    };

    // Starts a new titled group of switches in the main menu.
    void add_section(std::string title);
    // Adds a switch to the last section.
    void add_toggle(Toggle toggle);

    // Shows the main options dialog to the player.
    void open() const;

    // Per-feature menus.
    void open_auto_collect() const;
    void open_roulette() const;
    void open_wrench() const;
    void open_fast_drop() const;
    void open_mod_alert() const;
    void open_pathfind() const;
    void open_spam() const;
    void open_ping() const;

private:
    static constexpr const char* DIALOG_NAME{ "scarface_options" };

    struct Section {
        std::string title;
        std::vector<Toggle> toggles;
    };

    void on_dialog_return(const event::Event& event);
    // Opens the menu behind a settings button; false for an unknown button.
    bool open_settings(const std::string& button) const;
    [[nodiscard]] std::string build_dialog() const;
    void send_dialog(const std::string& dialog) const;
    void send_log(const std::string& msg) const;

    event::Dispatcher& dispatcher_;
    network::Server& server_;
    std::shared_ptr<Scheduler> scheduler_;
    QuickWrenchHandler& quick_wrench_;
    RouletteHandler& roulette_;
    ModAlertHandler& mod_alert_;
    FunFactsHandler& fun_facts_;
    FastDropHandler& fast_drop_;
    AutoCollectHandler& auto_collect_;
    HotkeyHandler& hotkey_;
    PathfindHandler& pathfind_;
    SpamHandler& spam_;
    NameTagHandler& ping_name_;
    VersionHandler& version_;

    std::vector<Section> sections_;
    // Checkbox values of the menu that was sent last (see send_dialog).
    mutable std::unordered_map<std::string, bool> shown_checkboxes_;
    std::unordered_map<std::string, std::function<void()>> settings_menus_;

    event::ScopedHandle handle_;
};
}
