#include "core.hpp"

#include <chrono>
#include <functional>
#include <thread>
#include <enet/enet.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "branding.hpp"

#include "../command/commands/auto_collect_command.hpp"
#include "../command/commands/balance_command.hpp"
#include "../command/commands/drop_all_locks_command.hpp"
#include "../command/commands/drop_command.hpp"
#include "../command/commands/spins_command.hpp"
#include "../command/commands/toggle_command.hpp"
#include "../command/commands/world_action_command.hpp"
#include "../command/commands/wrench_command.hpp"
#include "../command/commands/proxy_menu_command.hpp"
#include "../command/commands/nick_command.hpp"
#include "../command/commands/spam_command.hpp"
#include "../command/commands/count_command.hpp"
#include "../command/commands/collect_key_command.hpp"
#include "../command/commands/rmode_command.hpp"
#include "../item/locks.hpp"
#include "../packet/register_packets.hpp"
#include "../scripting/bindings/command_bindings.hpp"
#include "../scripting/bindings/event_bindings.hpp"
#include "../scripting/bindings/logger_bindings.hpp"
#include "../scripting/bindings/packet_bindings.hpp"
#include "../scripting/bindings/scheduler_bindings.hpp"
#include "../scripting/bindings/player_bindings.hpp"
#include "../scripting/bindings/world_bindings.hpp"
#include "../scripting/bindings/world_data_bindings.hpp"
#include "../scripting/bindings/item_bindings.hpp"

namespace core {
Core::Core()
    : running_{ true }
    , scheduler_{ std::make_shared<Scheduler>() }
{
    if (enet_initialize() != 0) {
        throw std::runtime_error{ "Failed to initialize ENet" };
    }

    server_ = std::make_unique<network::Server>(config_, dispatcher_);
    client_ = std::make_unique<network::Client>(config_, dispatcher_);
    web_server_ = std::make_unique<WebServer>(config_, dispatcher_, *client_, *server_);

    packet::register_all_packets();

    connection_handler_ = std::make_unique<handlers::ConnectionHandler>(dispatcher_, *client_, *server_, config_);
    forwarding_handler_ = std::make_unique<handlers::ForwardingHandler>(dispatcher_, *client_, *server_);
    version_handler_ = std::make_unique<handlers::VersionHandler>(dispatcher_, config_);
    world_handler_ = std::make_unique<handlers::WorldHandler>(dispatcher_);
    dialog_interceptor_ = std::make_unique<handlers::DialogInterceptor>(dispatcher_, scheduler_);
    inventory_handler_ = std::make_unique<handlers::InventoryHandler>(dispatcher_);
    auto_collect_handler_ = std::make_unique<handlers::AutoCollectHandler>(dispatcher_, *client_, scheduler_, config_);
    hotkey_handler_ = std::make_unique<handlers::HotkeyHandler>(scheduler_, *auto_collect_handler_, *server_, config_);
    pathfind_handler_ = std::make_unique<handlers::PathfindHandler>(dispatcher_, *client_, *server_, config_);
    drop_handler_ = std::make_unique<handlers::DropHandler>(*client_, *server_, *dialog_interceptor_, *auto_collect_handler_);
    world_manage_handler_ = std::make_unique<handlers::WorldManageHandler>(*client_, *server_, *dialog_interceptor_);
    quick_wrench_handler_ = std::make_unique<handlers::QuickWrenchHandler>(dispatcher_, *server_, *world_manage_handler_, config_);
    fast_drop_handler_ = std::make_unique<handlers::FastDropHandler>(dispatcher_, *client_, config_);
    fun_facts_handler_ = std::make_unique<handlers::FunFactsHandler>(scheduler_, *server_, config_);
    roulette_handler_ = std::make_unique<handlers::RouletteHandler>(dispatcher_, *server_, config_);
    mod_alert_handler_ = std::make_unique<handlers::ModAlertHandler>(dispatcher_, *server_, config_);
    spam_handler_ = std::make_unique<handlers::SpamHandler>(dispatcher_, scheduler_, *client_, *server_, config_);
    ping_name_handler_ = std::make_unique<handlers::PingNameHandler>(dispatcher_, scheduler_, *client_, *server_, config_);
    proxy_menu_handler_ = std::make_unique<handlers::ProxyMenuHandler>(dispatcher_, *server_, scheduler_, *quick_wrench_handler_, *roulette_handler_, *mod_alert_handler_, *fun_facts_handler_, *fast_drop_handler_, *auto_collect_handler_, *hotkey_handler_, *pathfind_handler_, *spam_handler_, *ping_name_handler_, *version_handler_);
    command_handler_ = std::make_unique<command::CommandHandler>(config_, dispatcher_, scheduler_, *server_, *client_);
    register_feature_commands();
    register_menu();
    start_discord_presence();

    script_engine_ = std::make_unique<scripting::LuaEngine>();

    script_scheduler_ = std::make_unique<scripting::ScriptScheduler>(*script_engine_);

    script_event_bridge_ = std::make_unique<scripting::ScriptEventBridge>(
        dispatcher_,
        *script_engine_,
        *client_,
        *server_
    );

    script_engine_->register_binding(std::make_unique<scripting::bindings::CommandBindings>(*command_handler_, *server_, *client_, dispatcher_, scheduler_));
    script_engine_->register_binding(std::make_unique<scripting::bindings::EventBindings>(*script_event_bridge_));
    script_engine_->register_binding(std::make_unique<scripting::bindings::LoggerBindings>());
    script_engine_->register_binding(std::make_unique<scripting::bindings::PacketBindings>(*client_, *server_));
    script_engine_->register_binding(std::make_unique<scripting::bindings::SchedulerBindings>(*script_scheduler_));
    script_engine_->register_binding(std::make_unique<scripting::bindings::PlayerBindings>());
    script_engine_->register_binding(std::make_unique<scripting::bindings::WorldBindings>());
    script_engine_->register_binding(std::make_unique<scripting::bindings::WorldDataBindings>());
    script_engine_->register_binding(std::make_unique<scripting::bindings::ItemBindings>());

    script_loader_ = std::make_unique<scripting::ScriptLoader>(*script_engine_, "scripts");
    script_loader_->load_all();

    spdlog::info("Core initialized successfully");
}

void Core::register_feature_commands()
{
    using Action = handlers::WorldManageHandler::Action;
    auto& registry{ command_handler_->registry() };

    registry.add(std::make_unique<command::AutoCollectCommand>(*auto_collect_handler_));
    registry.add(std::make_unique<command::BalanceCommand>());
    registry.add(std::make_unique<command::DropCommand>("wd", item::locks::WORLD_LOCK, "World Lock", *drop_handler_));
    registry.add(std::make_unique<command::DropCommand>("dd", item::locks::DIAMOND_LOCK, "Diamond Lock", *drop_handler_));
    registry.add(std::make_unique<command::DropCommand>("bd", item::locks::BLUE_GEM_LOCK, "Blue Gem Lock", *drop_handler_));
    registry.add(std::make_unique<command::DropAllLocksCommand>(*drop_handler_));
    registry.add(std::make_unique<command::WorldActionCommand>(Action::Pull, *world_manage_handler_));
    registry.add(std::make_unique<command::WorldActionCommand>(Action::Kick, *world_manage_handler_));
    registry.add(std::make_unique<command::WorldActionCommand>(Action::Ban, *world_manage_handler_));
    registry.add(std::make_unique<command::WrenchCommand>(*quick_wrench_handler_));
    registry.add(std::make_unique<command::ProxyMenuCommand>(*proxy_menu_handler_));
    registry.add(std::make_unique<command::CountCommand>(*fast_drop_handler_));
    registry.add(std::make_unique<command::CollectKeyCommand>(*hotkey_handler_));
    registry.add(std::make_unique<command::ToggleCommand>(
        "pathfind",
        "Teleport to the tile you click",
        "Pathfind teleport",
        [this] { return pathfind_handler_->is_enabled(); },
        [this](const bool enabled) { pathfind_handler_->set_enabled(enabled); }
    ));
    registry.add(std::make_unique<command::RmodeCommand>(*roulette_handler_));
    registry.add(std::make_unique<command::SpinsCommand>(*roulette_handler_));
    registry.add(std::make_unique<command::ToggleCommand>(
        "roulette",
        "Show roulette results before the wheel stops",
        "Early roulette results",
        [this] { return roulette_handler_->is_enabled(); },
        [this](const bool enabled) { roulette_handler_->set_enabled(enabled); }
    ));
    registry.add(std::make_unique<command::SpamCommand>(*spam_handler_));
    registry.add(std::make_unique<command::NickCommand>(
        [this](const std::string& name) { return ping_name_handler_->rename(name); }
    ));
    registry.add(std::make_unique<command::ToggleCommand>(
        "ping",
        "Show your ping next to your name",
        "Ping next to name",
        [this] { return ping_name_handler_->is_enabled(); },
        [this](const bool enabled) { ping_name_handler_->set_enabled(enabled); }
    ));
    registry.add(std::make_unique<command::ToggleCommand>(
        "modalert",
        "Warn when a moderator is in the world",
        "Mod alert",
        [this] { return mod_alert_handler_->is_enabled(); },
        [this](const bool enabled) { mod_alert_handler_->set_enabled(enabled); }
    ));

    // Typed without arguments, these open their settings menu.
    const auto menu{ [this](void (handlers::ProxyMenuHandler::*open)() const) {
        return [this, open] { (proxy_menu_handler_.get()->*open)(); };
    } };
    registry.set_menu("autocollect", menu(&handlers::ProxyMenuHandler::open_auto_collect));
    registry.set_menu("collectkey", menu(&handlers::ProxyMenuHandler::open_auto_collect));
    registry.set_menu("roulette", menu(&handlers::ProxyMenuHandler::open_roulette));
    registry.set_menu("rmode", menu(&handlers::ProxyMenuHandler::open_roulette));
    registry.set_menu("wrench", menu(&handlers::ProxyMenuHandler::open_wrench));
    registry.set_menu("count", menu(&handlers::ProxyMenuHandler::open_fast_drop));
    registry.set_menu("modalert", menu(&handlers::ProxyMenuHandler::open_mod_alert));
    registry.set_menu("pathfind", menu(&handlers::ProxyMenuHandler::open_pathfind));
    registry.set_menu("spam", menu(&handlers::ProxyMenuHandler::open_spam));
    registry.set_menu("ping", menu(&handlers::ProxyMenuHandler::open_ping));
}

void Core::register_menu()
{
    using Toggle = handlers::ProxyMenuHandler::Toggle;
    auto& menu{ *proxy_menu_handler_ };

    // Label colours follow the usual proxy menus: green "Enable", yellow
    // "Show", grey "Hide", the feature name in yellow.
    const auto toggle{ [](auto& handler) {
        return std::pair{
            std::function<bool()>{ [&handler] { return handler.is_enabled(); } },
            std::function<void(bool)>{ [&handler](const bool on) { handler.set_enabled(on); } },
        };
    } };
    const auto add{ [&menu](std::string id, std::string label, std::string description, auto accessors, std::string settings = {}) {
        menu.add_toggle(Toggle{
            std::move(id), std::move(label), std::move(description),
            std::move(accessors.first), std::move(accessors.second), std::move(settings)
        });
    } };

    menu.add_section("Automation");
    add("autocollect", "`2Enable `9Auto Collect",
        "Automatically Picks Up Items Dropped Near You. Set Range And Delay With `9Ayarla`` Or /autocollect.",
        toggle(*auto_collect_handler_), "menu_autocollect");
    add("spam", "`2Enable `9Auto Spam",
        "Says Your Message In World Chat Every Few Seconds. Stops By Itself If The Server Warns About Spam.",
        toggle(*spam_handler_), "menu_spam");
    add("fastdrop", "`2Enable `9Fast Drop",
        "Enables Fast Drop Without Additional Dialogs, `2/count [amount]`` For Specific Amount.",
        toggle(*fast_drop_handler_), "menu_fastdrop");

    menu.add_section("Roulette");
    add("wrench", "`2Enable `9Wrench `#Pull",
        "Enables Fast Wrenching. You Can Select Modes In `2/wrench``.",
        toggle(*quick_wrench_handler_), "menu_wrench");
    add("spin", "`9Show Real/Fake Spin",
        "Will Show You `2[REAL]`` and `4[FAKE]`` Roulette Wheel Spins.",
        toggle(*roulette_handler_), "menu_roulette");

    menu.add_section("Visuals");
    add("ping", "`9Show Ping Latency",
        "Will Display Your Ping Latency Next To Your Name.",
        toggle(*ping_name_handler_), "menu_ping");
    add("funfacts", "`9Show Fun Facts",
        "Will Regularly Show Fun Facts.",
        toggle(*fun_facts_handler_));

    menu.add_section("Safety");
    add("modalert", "`2Enable `9Auto Mod Detect",
        "Warns You When A Moderator Or An Invisible Player Is In The World.",
        toggle(*mod_alert_handler_));
    add("pathfind", "`2Enable `9Pathfinding Toggle",
        "You Can Teleport To Any Position By Clicking It. Type `2/pathfind`` To Change It.",
        toggle(*pathfind_handler_));
}

void Core::start_discord_presence()
{
    const auto& discord_config{ config_.get_discord_config() };
    if (!discord_config.enabled) {
        return;
    }

    std::string application_id{ discord_config.application_id };
#ifdef GTPROXY_DISCORD_APP_ID
    if (application_id.empty()) {
        application_id = GTPROXY_DISCORD_APP_ID;
    }
#endif

    utils::discord::Activity activity{};
    activity.details = "Waiting for Growtopia";
    activity.state = "Licensed Role: Premium Proxy User";
    activity.start_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    activity.large_image = discord_config.large_image;
    activity.large_text = fmt::format("Scarface Premium Proxy {}", branding::VERSION);
    activity.small_image = discord_config.small_image;
    activity.small_text = "Licensed";
    activity.buttons = {
        { "Scarface Premium Proxy Discord", discord_config.discord_url },
        { "Scarface Premium Proxy Website", discord_config.website_url },
    };

    discord_presence_ = std::make_unique<DiscordPresence>(std::move(application_id), std::move(activity));

    // The game connecting to the proxy is the closest thing to "in game".
    presence_handles_.emplace_back(
        dispatcher_,
        event::Type::ClientConnect,
        dispatcher_.append_listener(event::Type::ClientConnect, [this](const event::Event&) {
            discord_presence_->set_details("Currently In-game");
        })
    );
    presence_handles_.emplace_back(
        dispatcher_,
        event::Type::ClientDisconnect,
        dispatcher_.append_listener(event::Type::ClientDisconnect, [this](const event::Event&) {
            discord_presence_->set_details("Waiting for Growtopia");
        })
    );
}

Core::~Core()
{
    enet_deinitialize();
}

void Core::run() const
{
    constexpr auto sleep_timer = std::chrono::microseconds{ 5000 };
    auto prev = std::chrono::high_resolution_clock::now();
    auto sleep_duration = sleep_timer;

    while (running_) {
        const auto now = std::chrono::high_resolution_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - prev);
        prev = now;

        sleep_duration += sleep_timer - elapsed;

        server_->process();
        client_->process();
        scheduler_->run_main_thread_tasks();
        script_scheduler_->update(elapsed);

        if (sleep_duration > std::chrono::microseconds::zero()) {
            std::this_thread::sleep_for(sleep_duration);
        }

        if (sleep_duration >= sleep_timer) {
            sleep_duration = std::chrono::microseconds::zero();
        }
    }
}
}
