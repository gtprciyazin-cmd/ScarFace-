#pragma once
#include <string>
#include <vector>

namespace core {
class Config {
public:
    struct ServerConfig {
        int port{ 16999 };
        std::string address{ "www.growtopia1.com" };
    };

    struct ClientConfig {
        std::string game_version{ "5.39" };
        int protocol{ 225 };
        std::string dns_server{ "cloudflare" };
    };

    struct LogConfig {
        bool print_message{ true };
        bool print_game_update_packet{ false };
        bool print_variant{ true };
        bool print_extra{ true };
    };

    struct CommandConfig {
        char prefix{ '/' };
    };

    // Toggles changed with proxy commands; saved so they survive a restart.
    struct FeatureConfig {
        bool auto_collect{ false };
        float auto_collect_range{ 2.0f };
        int auto_collect_delay_ms{ 0 };
        bool roulette{ true };
        std::string roulette_mode{ "reme" };
        bool mod_alert{ true };
        bool quick_wrench{ false };
        std::string quick_wrench_action{ "pull" };
        bool fun_facts{ true };
        bool fast_drop{ false };
        int fast_drop_amount{ 0 };
        bool autocollect_keybind{ false };
        int autocollect_key{ 117 }; // Windows VK_F6
        bool pathfind{ false };
        std::string spam_text{};
        int spam_interval_s{ 10 };
        bool ping_in_name{ true };
        bool gem_count{ false };
        bool public_gates{ false };
        bool door_id{ true };
        bool instant_dice{ true };
        bool fast_trash{ false };
        bool auto_access{ false };
        bool spin_tags{ true };
        bool reme_numbers{ true };
        bool last_spin_names{ false };
        bool hide_private_messages{ false };
        bool hide_level_text{ false };
        bool consumable_autoban{ false };
        bool anti_lava{ false };
        bool antipunch{ false };
        bool zoom_out{ false };
        float zoom_level{ 0.15f };
        // Legit mode: random gap between the proxy's own actions (see LegitManager).
        bool legit_mode{ true };
        int legit_min_delay_ms{ 80 };
        int legit_max_delay_ms{ 250 };
        // Snowball, Freeze Wand, Mud Glob, Rotten Egg, Egg, Tomato, Water
        // Balloon, Lemon, Love Potion #8, Doppelganger Potion, g-Virus,
        // Psychotic Bunny, War Balloons, Ghost-In-A-Jar, Mind-Ghost-In-A-Jar.
        std::vector<int> autoban_consumables{ 1368, 274, 368, 614, 874, 962, 3064, 4666, 3400, 1988, 764, 618, 4844, 4846, 4848, 3722, 6080 };
    };

    // Discord Rich Presence. The application id picks the Discord app whose
    // name ("Scarface Premium Proxy") and uploaded art are shown; empty uses
    // the one built into the exe.
    struct DiscordConfig {
        bool enabled{ true };
        std::string application_id{};
        std::string large_image{ "scarface" };
        std::string small_image{ "verified" };
        std::string discord_url{};
        std::string website_url{};
    };

    // Saved once the key is validated, so it is not asked for again.
    struct LicenseConfig {
        std::string key;
    };

    struct WrapperConfig {
        ServerConfig server;
        ClientConfig client;
        LogConfig log;
        CommandConfig command;
        FeatureConfig features;
        DiscordConfig discord;
        LicenseConfig license;
    };

public:
    Config();
    ~Config() = default;

public:
    [[nodiscard]] const ServerConfig& get_server_config() const { return config_.server; }
    [[nodiscard]] const ClientConfig& get_client_config() const { return config_.client; }
    [[nodiscard]] ClientConfig& get_client_config() { return config_.client; }
    [[nodiscard]] const LogConfig& get_log_config() const { return config_.log; }
    [[nodiscard]] const CommandConfig& get_command_config() const { return config_.command; }
    [[nodiscard]] const FeatureConfig& get_feature_config() const { return config_.features; }
    [[nodiscard]] FeatureConfig& get_feature_config() { return config_.features; }
    [[nodiscard]] const DiscordConfig& get_discord_config() const { return config_.discord; }
    [[nodiscard]] const LicenseConfig& get_license_config() const { return config_.license; }
    [[nodiscard]] LicenseConfig& get_license_config() { return config_.license; }

    // Writes the current configuration to config.json. Returns false on failure.
    bool save() const;

private:
    WrapperConfig config_;
};
}
