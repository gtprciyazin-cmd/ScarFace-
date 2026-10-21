#include "proxy_menu_handler.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../../packet/game/dialog.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/message/dialog_return.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../utils/chat_text.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    constexpr auto dialog_return_type{ event::packet_event_type(packet::PacketId::DialogReturn) };

    [[nodiscard]] std::string_view onoff(const bool enabled)
    {
        return enabled ? "1" : "0";
    }

    // Item ids whose icons stand for each feature in the menus.
    namespace icons {
        constexpr int SCARFACE{ 7188 };      // Blue Gem Lock
        constexpr int AUTO_COLLECT{ 8304 };  // Magic Magnet
        constexpr int SPAM{ 2480 };          // Megaphone
        constexpr int PING{ 226 };           // Signal Jammer
        constexpr int ROULETTE{ 758 };       // Roulette Wheel
        constexpr int WRENCH{ 32 };          // Wrench
        constexpr int FAST_DROP{ 242 };      // World Lock
        constexpr int MOD_ALERT{ 2976 };     // Cybernetic Eye
        constexpr int PATHFIND{ 6526 };      // Teleporter Charge
        constexpr int FUN_FACTS{ 550 };      // Bookcase
    }

    struct Feature {
        int icon;
        std::string_view title;
        std::string_view description;
        std::string_view checkbox;
        bool enabled;
        // Button that opens the feature's own menu; null when it has none.
        const char* settings_button;
    };

    [[nodiscard]] std::string ping_text(const std::uint32_t ping_ms)
    {
        return ping_ms == 0 ? std::string{ "`7-``" } : utils::format_ping(ping_ms);
    }

    // Scarface colours (dark panel, red frame), icon and name, a grey line
    // under them, then a gap.
    void add_header(std::string& d, const int icon, const std::string_view title, const std::string_view subtitle)
    {
        d += "set_bg_color|18,18,28,235|\n";
        d += "set_border_color|200,40,40,255|\n";
        d += "set_default_color|`o\n";
        d += fmt::format("add_label_with_icon|big|`w{}``|left|{}|\n", title, icon);
        if (!subtitle.empty()) {
            d += fmt::format("add_smalltext|`s{}``|left|\n", subtitle);
        }
        d += "add_spacer|small|\n";
    }

    // One feature of the main menu: name and description side by side, the
    // switch under them and, when it has more settings, a button to its menu.
    void add_feature(std::string& d, const Feature& f)
    {
        d += fmt::format("add_label_with_icon|small|`w{}`` `s- {}``|left|{}|\n", f.title, f.description, f.icon);
        d += fmt::format("add_checkbox|{}|`2Aktif``|{}\n", f.checkbox, onoff(f.enabled));
        if (f.settings_button) {
            d += fmt::format("add_button|{}|`9Ayarla``|noflags|0|0|\n", f.settings_button);
        }
        d += "add_spacer|small|\n";
    }

    // Detail menus get a button back to the main menu; Save applies either way.
    void add_footer(std::string& d, const std::string_view dialog_name, const bool back_button)
    {
        if (back_button) {
            d += "add_spacer|small|\n";
            d += "add_button|menu_back|`wGeri``|noflags|0|0|\n";
        }
        d += "add_quick_exit|\n";
        d += fmt::format("end_dialog|{}|Kapat|Kaydet|\n", dialog_name);
    }

    template <typename T>
    [[nodiscard]] std::optional<T> parse_number(const std::string_view text)
    {
        T value{};
        const auto* end{ text.data() + text.size() };
        if (const auto [ptr, ec]{ std::from_chars(text.data(), end, value) }; text.empty() || ec != std::errc{} || ptr != end) {
            return std::nullopt;
        }
        return value;
    }
}

ProxyMenuHandler::ProxyMenuHandler(
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
    PingNameHandler& ping_name,
    VersionHandler& version
)
    : dispatcher_{ dispatcher }
    , server_{ server }
    , scheduler_{ std::move(scheduler) }
    , quick_wrench_{ quick_wrench }
    , roulette_{ roulette }
    , mod_alert_{ mod_alert }
    , fun_facts_{ fun_facts }
    , fast_drop_{ fast_drop }
    , auto_collect_{ auto_collect }
    , hotkey_{ hotkey }
    , pathfind_{ pathfind }
    , spam_{ spam }
    , ping_name_{ ping_name }
    , version_{ version }
{
    handle_ = event::ScopedHandle{
        dispatcher_,
        dialog_return_type,
        dispatcher_.append_listener(dialog_return_type, [this](const event::Event& e) { on_dialog_return(e); })
    };
}

std::string ProxyMenuHandler::build_dialog() const
{
    std::string subtitle{ fmt::format("Ping: {}", ping_text(ping_name_.get_ping())) };
    if (const auto& gv{ version_.get_game_version() }; !gv.empty()) {
        subtitle = fmt::format("Growtopia v{} - {}", gv, subtitle);
    }

    std::string d{};
    add_header(d, icons::SCARFACE, "Scarface Proxy `9v3.0", subtitle);
    d += "add_textbox|`oOzellikleri `2Aktif`` ile ac/kapat, `9Ayarla`` ile detaylarini degistir. Degisiklikler `wKaydet`` ile uygulanir.``|left|\n";
    d += "add_spacer|small|\n";

    add_feature(d, { icons::AUTO_COLLECT, "Auto Collect", "Yere dusen esyalari otomatik toplar", "cb_autocollect", auto_collect_.is_enabled(), "menu_autocollect" });
    add_feature(d, { icons::SPAM, "Spam", "Mesajini belirledigin aralikla atar", "cb_spam", spam_.is_enabled(), "menu_spam" });
    add_feature(d, { icons::PING, "Ping", "Isminin yaninda pingini gosterir", "cb_ping", ping_name_.is_enabled(), "menu_ping" });
    add_feature(d, { icons::ROULETTE, "Real/Fake Spin", "Rulet sonucunu erkenden gosterir", "cb_spin", roulette_.is_enabled(), "menu_roulette" });
    add_feature(d, { icons::WRENCH, "Wrench Pull", "Wrench ile menusuz pull/kick/ban", "cb_wrench", quick_wrench_.is_enabled(), "menu_wrench" });
    add_feature(d, { icons::FAST_DROP, "Fast Drop", "Drop penceresini sormadan atar", "cb_fastdrop", fast_drop_.is_enabled(), "menu_fastdrop" });
    add_feature(d, { icons::MOD_ALERT, "Mod Alert", "Dunyaya mod girince uyarir", "cb_modalert", mod_alert_.is_enabled(), nullptr });
    add_feature(d, { icons::PATHFIND, "Pathfind", "Tikladigin yere isinlar", "cb_pathfind", pathfind_.is_enabled(), nullptr });
    add_feature(d, { icons::FUN_FACTS, "Fun Facts", "Konsola ilginc bilgiler yazar", "cb_funfacts", fun_facts_.is_enabled(), nullptr });

    d += "add_label|small|`9Komutlar``|left|\n";
    d += "add_smalltext|`s/daw: envanterdeki tum WL/DL/BGL'leri atar. /wd /dd /bd <adet>: kilit atar.``|left|\n";
    d += "add_smalltext|`sBir ozelligin komutunu tek basina yazarsan (/spam, /ping, /autocollect...) menusu acilir.``|left|\n";
    add_footer(d, DIALOG_NAME, false);
    return d;
}

void ProxyMenuHandler::open() const
{
    send_dialog(build_dialog());
}

void ProxyMenuHandler::open_auto_collect() const
{
    std::string d{};
    add_header(d, icons::AUTO_COLLECT, "Auto Collect", "Yakindaki dusen esyalari otomatik toplar.");
    d += fmt::format("add_checkbox|ac_enabled|`wAuto Collect acik``|{}\n", onoff(auto_collect_.is_enabled()));
    d += "add_spacer|small|\n";
    d += fmt::format(
        "add_text_input|ac_range|Menzil (blok, {:.0f}-{:.0f})|{:g}|4|\n",
        AutoCollectHandler::MIN_RANGE,
        AutoCollectHandler::MAX_RANGE,
        auto_collect_.get_range()
    );
    d += "add_smalltext|`sKac blok uzaktaki esyalarin toplanacagi.``|left|\n";
    d += fmt::format(
        "add_text_input|ac_delay|Gecikme (ms, {}-{})|{}|5|\n",
        AutoCollectHandler::MIN_DELAY_MS,
        AutoCollectHandler::MAX_DELAY_MS,
        auto_collect_.get_delay_ms()
    );
    d += "add_smalltext|`sIki toplama arasindaki bekleme. 0 ms: hepsini hemen toplar.``|left|\n";
    d += "add_spacer|small|\n";
    d += fmt::format("add_checkbox|ac_hotkey|`wKisayol tusu ile ac/kapat``|{}\n", onoff(hotkey_.is_enabled()));
    d += fmt::format("add_text_input|ac_key|Kisayol tusu|{}|6|\n", HotkeyHandler::name_from_key(hotkey_.get_key()));
    d += "add_smalltext|`sF1-F12, A-Z ya da 0-9. Tus sadece Growtopia penceresi aktifken calisir.``|left|\n";
    add_footer(d, "scarface_autocollect", true);
    send_dialog(d);
}

void ProxyMenuHandler::open_spam() const
{
    std::string d{};
    add_header(d, icons::SPAM, "Spam", "Yazdigin mesaji belirledigin aralikla sohbete atar.");
    d += fmt::format(
        "add_label|small|Durum: {}``|left|\n",
        spam_.is_enabled() ? fmt::format("`2Calisiyor ({} sn)", spam_.get_interval()) : std::string{ "`4Kapali" }
    );
    d += "add_spacer|small|\n";
    d += fmt::format("add_text_input|sp_text|Mesaj|{}|{}|\n", spam_.get_text(), SpamHandler::MAX_LENGTH);
    d += fmt::format(
        "add_text_input|sp_interval|Aralik (saniye, {}-{})|{}|4|\n",
        SpamHandler::MIN_INTERVAL_S,
        SpamHandler::MAX_INTERVAL_S,
        spam_.get_interval()
    );
    d += "add_smalltext|`sSadece bir dunyanin icindeyken yazar. Sunucu spam uyarisi verirse kendiliginden durur.``|left|\n";
    d += "add_smalltext|`sProxy kapaninca spam da kapanir; mesaj ve aralik kayitli kalir.``|left|\n";
    d += "add_spacer|small|\n";
    d += spam_.is_enabled()
        ? "add_button|sp_toggle|`4DURDUR``|noflags|0|0|\n"
        : "add_button|sp_toggle|`2BASLAT``|noflags|0|0|\n";
    add_footer(d, "scarface_spam", true);
    send_dialog(d);
}

void ProxyMenuHandler::open_ping() const
{
    std::string d{};
    add_header(d, icons::PING, "Ping", fmt::format("Su anki ping: {}", ping_text(ping_name_.get_ping())));
    d += fmt::format("add_checkbox|pg_enabled|`wIsminin yaninda ping goster``|{}\n", onoff(ping_name_.is_enabled()));
    d += "add_smalltext|`sIsminin yanina sunucuya olan gecikmen yazilir ve 2 saniyede bir guncellenir.``|left|\n";
    d += "add_smalltext|`2Yesil`` 100 ms alti, `9sari`` 200 ms alti, `4kirmizi`` daha yavas. `7Sadece sen gorursun.``|left|\n";
    add_footer(d, "scarface_ping", true);
    send_dialog(d);
}

void ProxyMenuHandler::open_roulette() const
{
    std::string d{};
    add_header(d, icons::ROULETTE, "Real/Fake Spin", "Rulet sonucunu carkin uzerinde erkenden gosterir.");
    d += fmt::format("add_checkbox|rl_enabled|`wSonucu erken goster``|{}\n", onoff(roulette_.is_enabled()));
    d += "add_spacer|small|\n";
    d += fmt::format("add_text_input|rl_mode|Mod|{}|4|\n", roulette_.mode_string());
    d += "add_smalltext|`sreal: sayinin kendisi, reme: rakamlar toplami (32 -> 5), qq: son rakam (27 -> 7).``|left|\n";
    add_footer(d, "scarface_roulette", true);
    send_dialog(d);
}

void ProxyMenuHandler::open_wrench() const
{
    std::string d{};
    add_header(d, icons::WRENCH, "Wrench Pull", "Oyuncuya wrench atinca islemi menusuz yapar.");
    d += fmt::format("add_checkbox|wr_enabled|`wWrench ile hizli islem``|{}\n", onoff(quick_wrench_.is_enabled()));
    d += "add_spacer|small|\n";
    d += fmt::format("add_text_input|wr_action|Islem|{}|4|\n", QuickWrenchHandler::action_to_string(quick_wrench_.get_action()));
    d += "add_smalltext|`spull, kick ya da ban. Sadece kendi kilitli dunyanda calisir.``|left|\n";
    add_footer(d, "scarface_wrench", true);
    send_dialog(d);
}

void ProxyMenuHandler::open_fast_drop() const
{
    std::string d{};
    add_header(d, icons::FAST_DROP, "Fast Drop", "Drop penceresini sormadan esyayi atar.");
    d += fmt::format("add_checkbox|fd_enabled|`wFast Drop acik``|{}\n", onoff(fast_drop_.is_enabled()));
    d += "add_spacer|small|\n";
    d += fmt::format("add_text_input|fd_amount|Miktar|{}|3|\n", fast_drop_.get_amount());
    d += "add_smalltext|`s0 yazarsan esyanin hepsini atar.``|left|\n";
    add_footer(d, "scarface_fastdrop", true);
    send_dialog(d);
}

void ProxyMenuHandler::open_mod_alert() const
{
    std::string d{};
    add_header(d, icons::MOD_ALERT, "Mod Alert", "Dunyaya moderator ya da gorunmez oyuncu girince uyarir.");
    d += fmt::format("add_checkbox|ma_enabled|`wUyari acik``|{}\n", onoff(mod_alert_.is_enabled()));
    add_footer(d, "scarface_modalert", true);
    send_dialog(d);
}

void ProxyMenuHandler::open_pathfind() const
{
    std::string d{};
    add_header(d, icons::PATHFIND, "Pathfind", "Tikladigin yere isinlar.");
    d += fmt::format("add_checkbox|pf_enabled|`wTiklanan yere isinlan``|{}\n", onoff(pathfind_.is_enabled()));
    d += "add_smalltext|`sDeneysel: sunucu uzak isinlanmalari reddedebilir.``|left|\n";
    add_footer(d, "scarface_pathfind", true);
    send_dialog(d);
}

void ProxyMenuHandler::send_dialog(const std::string& dialog) const
{
    // The menu is opened from chat, so the client may still be closing the chat
    // box when a local reply arrives; it drops dialogs while another menu is
    // open. Real servers answer after a network round trip, so wait as long.
    const auto scheduler{ scheduler_.get() };
    scheduler_->schedule_delayed(
        [this, scheduler, dialog] {
            scheduler->post_to_main_thread([this, dialog] {
                // Shown in the console even when the client refuses the dialog, so
                // a missing menu can be told apart from a command that never ran.
                send_log("`9[Scarface]`` Menu aciliyor...");
                if (world::World::instance().get_local_net_id() < 0) {
                    send_log("`4[Scarface]`` Menu sadece bir dunyanin icinde acilir.");
                }

                packet::game::OnDialogRequest pkt{};
                pkt.dialog = dialog;
                if (!packet::PacketHelper::write(pkt, server_)) {
                    spdlog::warn("Proxy menu: could not send the dialog (client not connected)");
                    return;
                }

                spdlog::info("Proxy menu: sent dialog ({} bytes)", dialog.size());
            });
        },
        std::chrono::milliseconds{ 200 },
        "proxy_menu"
    );
}

void ProxyMenuHandler::on_dialog_return(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::DialogReturn>*>(&event) };
    if (!evt || evt->direction != event::Direction::ServerBound) {
        return;
    }

    const auto pkt{ evt->get<packet::message::DialogReturn>() };
    if (!pkt || !pkt->dialog_name.starts_with("scarface_")) {
        return;
    }

    // Our own injected dialog: apply it and keep it from reaching the server.
    evt->cancel();

    const auto& fields{ pkt->text_parse };
    const auto field{ [&fields](const char* key) { return fields.get<std::string>(key); } };
    const auto apply{ [&field](const char* key, auto& handler) {
        const auto raw{ field(key) };
        if (raw.empty()) {
            return; // Field absent: leave this feature unchanged.
        }

        handler.set_enabled(raw == "1");
    } };
    const auto lower{ [](std::string text) {
        std::ranges::transform(text, text.begin(), [](const unsigned char c) { return std::tolower(c); });
        return text;
    } };

    const auto& name{ pkt->dialog_name };
    if (name == DIALOG_NAME) {
        apply("cb_wrench", quick_wrench_);
        apply("cb_spin", roulette_);
        apply("cb_modalert", mod_alert_);
        apply("cb_funfacts", fun_facts_);
        apply("cb_fastdrop", fast_drop_);
        apply("cb_autocollect", auto_collect_);
        apply("cb_pathfind", pathfind_);
        apply("cb_spam", spam_);
        apply("cb_ping", ping_name_);

        // A button opens a detailed menu (the checkboxes above are kept).
        const auto button{ field("buttonClicked") };
        if (button == "menu_autocollect") {
            open_auto_collect();
            return;
        }
        if (button == "menu_spam") {
            open_spam();
            return;
        }
        if (button == "menu_ping") {
            open_ping();
            return;
        }
        if (button == "menu_roulette") {
            open_roulette();
            return;
        }
        if (button == "menu_wrench") {
            open_wrench();
            return;
        }
        if (button == "menu_fastdrop") {
            open_fast_drop();
            return;
        }
    }
    else if (name == "scarface_spam") {
        // Text and interval first, so starting uses the new message.
        if (fields.contains("sp_text")) {
            std::ignore = spam_.set_text(field("sp_text"));
        }
        if (const auto interval{ parse_number<int>(field("sp_interval")) }) {
            std::ignore = spam_.set_interval(*interval);
        }

        if (field("buttonClicked") == "sp_toggle") {
            const bool start{ !spam_.is_enabled() };
            spam_.set_enabled(start);
            if (start && !spam_.is_enabled()) {
                return; // No message to send; set_enabled told the player.
            }

            spdlog::info("Proxy menu: spam {}", start ? "started" : "stopped");
            send_log(spam_.is_enabled()
                ? fmt::format("`2Spam basladi:`` her {} saniyede bir yazilacak.", spam_.get_interval())
                : std::string{ "`4Spam durduruldu." });
            return;
        }
    }
    else if (name == "scarface_ping") {
        apply("pg_enabled", ping_name_);
    }
    else if (name == "scarface_autocollect") {
        apply("ac_enabled", auto_collect_);
        apply("ac_hotkey", hotkey_);
        if (const auto range{ parse_number<float>(field("ac_range")) }) {
            auto_collect_.set_range(*range);
        }
        if (const auto delay{ parse_number<int>(field("ac_delay")) }) {
            auto_collect_.set_delay_ms(*delay);
        }
        if (const auto key_name{ field("ac_key") }; !key_name.empty()) {
            if (const int vk{ HotkeyHandler::key_from_name(key_name) }; vk > 0) {
                hotkey_.set_key(vk);
            }
            else {
                send_log(fmt::format("`4Bilinmeyen tus: ``{}", key_name));
            }
        }
    }
    else if (name == "scarface_roulette") {
        apply("rl_enabled", roulette_);
        if (const auto mode{ lower(field("rl_mode")) }; mode == "real" || mode == "reme" || mode == "qq") {
            roulette_.set_mode_from_string(mode);
        }
    }
    else if (name == "scarface_wrench") {
        apply("wr_enabled", quick_wrench_);
        if (const auto action{ lower(field("wr_action")) }; action == "pull" || action == "kick" || action == "ban" || action == "cek") {
            quick_wrench_.set_action(QuickWrenchHandler::action_from_string(action));
        }
    }
    else if (name == "scarface_fastdrop") {
        apply("fd_enabled", fast_drop_);
        if (const auto amount{ parse_number<int>(field("fd_amount")) }; amount && *amount >= 0) {
            fast_drop_.set_amount(*amount);
        }
    }
    else if (name == "scarface_modalert") {
        apply("ma_enabled", mod_alert_);
    }
    else if (name == "scarface_pathfind") {
        apply("pf_enabled", pathfind_);
    }
    else {
        return;
    }

    spdlog::info("Proxy menu: {} applied", name);
    if (name != DIALOG_NAME && field("buttonClicked") == "menu_back") {
        open();
        return;
    }

    send_log("`2Scarface Proxy ayarlari kaydedildi.");
}

void ProxyMenuHandler::send_log(const std::string& msg) const
{
    packet::message::Log log_pkt{};
    log_pkt.msg = msg;
    std::ignore = packet::PacketHelper::write(log_pkt, server_);
}
}
