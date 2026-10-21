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
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    constexpr auto dialog_return_type{ event::packet_event_type(packet::PacketId::DialogReturn) };

    [[nodiscard]] std::string_view onoff(const bool enabled)
    {
        return enabled ? "1" : "0";
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
    std::string d{};
    d += "set_default_color|`o\n";
    d += "add_label_with_icon|big|`wScarface Proxy``|left|32|\n";
    if (const auto& gv{ version_.get_game_version() }; !gv.empty()) {
        d += fmt::format("add_textbox|`oGrowtopia v{} `7(protocol {})``|left|\n", gv, version_.get_protocol());
    }
    d += "add_spacer|small|\n";
    d += fmt::format("add_checkbox|cb_wrench|`wWrench Pull``|{}\n", onoff(quick_wrench_.is_enabled()));
    d += fmt::format("add_checkbox|cb_spin|`wShow Real/Fake Spin``|{}\n", onoff(roulette_.is_enabled()));
    d += fmt::format("add_checkbox|cb_modalert|`wAuto Mod Detect``|{}\n", onoff(mod_alert_.is_enabled()));
    d += fmt::format("add_checkbox|cb_funfacts|`wShow Fun Facts``|{}\n", onoff(fun_facts_.is_enabled()));
    d += fmt::format("add_checkbox|cb_fastdrop|`wFast Drop``|{}\n", onoff(fast_drop_.is_enabled()));
    d += fmt::format("add_checkbox|cb_autocollect|`wAuto Collect``|{}\n", onoff(auto_collect_.is_enabled()));
    d += fmt::format("add_checkbox|cb_pathfind|`wPathfind (click to teleport)``|{}\n", onoff(pathfind_.is_enabled()));
    d += "add_spacer|small|\n";
    d += "add_textbox|`oAyrintili ayarlar:``|left|\n";
    d += "add_button|menu_autocollect|`wAuto Collect``|noflags|0|0|\n";
    d += "add_button|menu_roulette|`wRulet``|noflags|0|0|\n";
    d += "add_button|menu_wrench|`wWrench``|noflags|0|0|\n";
    d += "add_button|menu_fastdrop|`wFast Drop``|noflags|0|0|\n";
    d += "add_quick_exit|\n";
    d += "end_dialog|scarface_options|Kapat|Kaydet|\n";
    return d;
}

void ProxyMenuHandler::open() const
{
    send_dialog(build_dialog());
}

void ProxyMenuHandler::open_auto_collect() const
{
    std::string d{};
    d += "set_default_color|`o\n";
    d += "add_label_with_icon|big|`wAuto Collect``|left|112|\n";
    d += "add_spacer|small|\n";
    d += fmt::format("add_checkbox|ac_enabled|`wAuto Collect``|{}\n", onoff(auto_collect_.is_enabled()));
    d += fmt::format(
        "add_text_input|ac_range|Menzil (blok, {:.0f}-{:.0f})|{:g}|4|\n",
        AutoCollectHandler::MIN_RANGE,
        AutoCollectHandler::MAX_RANGE,
        auto_collect_.get_range()
    );
    d += fmt::format(
        "add_text_input|ac_delay|Gecikme (ms, {}-{})|{}|5|\n",
        AutoCollectHandler::MIN_DELAY_MS,
        AutoCollectHandler::MAX_DELAY_MS,
        auto_collect_.get_delay_ms()
    );
    d += "add_textbox|`o0 ms: yakindaki esyalari hemen toplar.``|left|\n";
    d += "add_spacer|small|\n";
    d += fmt::format("add_checkbox|ac_hotkey|`wKisayol tusu ile ac/kapat``|{}\n", onoff(hotkey_.is_enabled()));
    d += fmt::format("add_text_input|ac_key|Kisayol tusu (F1-F12, A-Z, 0-9)|{}|6|\n", HotkeyHandler::name_from_key(hotkey_.get_key()));
    d += "add_textbox|`oTus sadece Growtopia penceresi aktifken calisir.``|left|\n";
    d += "add_quick_exit|\n";
    d += "end_dialog|scarface_autocollect|Kapat|Kaydet|\n";
    send_dialog(d);
}

void ProxyMenuHandler::open_roulette() const
{
    std::string d{};
    d += "set_default_color|`o\n";
    d += "add_label_with_icon|big|`wRulet``|left|758|\n";
    d += "add_spacer|small|\n";
    d += fmt::format("add_checkbox|rl_enabled|`wSonucu erken goster``|{}\n", onoff(roulette_.is_enabled()));
    d += fmt::format("add_text_input|rl_mode|Mod (real / reme / qq)|{}|4|\n", roulette_.mode_string());
    d += "add_textbox|`oreal: sayi, reme: rakamlar toplami (32->5), qq: son rakam (27->7).``|left|\n";
    d += "add_quick_exit|\n";
    d += "end_dialog|scarface_roulette|Kapat|Kaydet|\n";
    send_dialog(d);
}

void ProxyMenuHandler::open_wrench() const
{
    std::string d{};
    d += "set_default_color|`o\n";
    d += "add_label_with_icon|big|`wWrench``|left|32|\n";
    d += "add_spacer|small|\n";
    d += fmt::format("add_checkbox|wr_enabled|`wWrench ile hizli islem``|{}\n", onoff(quick_wrench_.is_enabled()));
    d += fmt::format("add_text_input|wr_action|Islem (pull / kick / ban)|{}|4|\n", QuickWrenchHandler::action_to_string(quick_wrench_.get_action()));
    d += "add_textbox|`oAcikken dunyandaki bir oyuncuya wrench atinca bu islem menusuz yapilir.``|left|\n";
    d += "add_quick_exit|\n";
    d += "end_dialog|scarface_wrench|Kapat|Kaydet|\n";
    send_dialog(d);
}

void ProxyMenuHandler::open_fast_drop() const
{
    std::string d{};
    d += "set_default_color|`o\n";
    d += "add_label_with_icon|big|`wFast Drop``|left|242|\n";
    d += "add_spacer|small|\n";
    d += fmt::format("add_checkbox|fd_enabled|`wFast Drop``|{}\n", onoff(fast_drop_.is_enabled()));
    d += fmt::format("add_text_input|fd_amount|Miktar (0 = hepsi)|{}|3|\n", fast_drop_.get_amount());
    d += "add_textbox|`oAcikken drop menusu sormadan bu kadarini atar.``|left|\n";
    d += "add_quick_exit|\n";
    d += "end_dialog|scarface_fastdrop|Kapat|Kaydet|\n";
    send_dialog(d);
}

void ProxyMenuHandler::open_mod_alert() const
{
    std::string d{};
    d += "set_default_color|`o\n";
    d += "add_label_with_icon|big|`wMod Alert``|left|278|\n";
    d += "add_spacer|small|\n";
    d += fmt::format("add_checkbox|ma_enabled|`wModerator / gorunmez oyuncu uyarisi``|{}\n", onoff(mod_alert_.is_enabled()));
    d += "add_quick_exit|\n";
    d += "end_dialog|scarface_modalert|Kapat|Kaydet|\n";
    send_dialog(d);
}

void ProxyMenuHandler::open_pathfind() const
{
    std::string d{};
    d += "set_default_color|`o\n";
    d += "add_label_with_icon|big|`wPathfind``|left|1404|\n";
    d += "add_spacer|small|\n";
    d += fmt::format("add_checkbox|pf_enabled|`wTiklanan yere isinlan``|{}\n", onoff(pathfind_.is_enabled()));
    d += "add_textbox|`oDeneysel: sunucu uzak isinlanmalari reddedebilir.``|left|\n";
    d += "add_quick_exit|\n";
    d += "end_dialog|scarface_pathfind|Kapat|Kaydet|\n";
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

        // A button opens a detailed menu (the checkboxes above are kept).
        const auto button{ field("buttonClicked") };
        if (button == "menu_autocollect") {
            open_auto_collect();
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
    send_log("`2Scarface Proxy ayarlari kaydedildi.");
}

void ProxyMenuHandler::send_log(const std::string& msg) const
{
    packet::message::Log log_pkt{};
    log_pkt.msg = msg;
    std::ignore = packet::PacketHelper::write(log_pkt, server_);
}
}
