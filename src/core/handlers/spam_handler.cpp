#include "spam_handler.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <tuple>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../../packet/game/message.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/game/world.hpp"
#include "../../packet/message/input.hpp"
#include "../../packet/message/raw_text.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../utils/chat_text.hpp"
#include "../../utils/color_codes.hpp"
#include "../../utils/game_text.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
    constexpr auto console_message_type{ event::packet_event_type(packet::PacketId::OnConsoleMessage) };
    constexpr auto input_type{ event::packet_event_type(packet::PacketId::Input) };
    constexpr auto spawn_type{ event::packet_event_type(packet::PacketId::OnSpawn) };

    [[nodiscard]] std::string lowercase(std::string text)
    {
        std::ranges::transform(text, text.begin(), [](const unsigned char c) { return std::tolower(c); });
        return text;
    }
}

SpamHandler::SpamHandler(
    event::Dispatcher& dispatcher,
    std::shared_ptr<Scheduler> scheduler,
    network::Client& client,
    network::Server& server,
    Config& config
)
    : dispatcher_{ dispatcher }
    , scheduler_{ std::move(scheduler) }
    , client_{ client }
    , server_{ server }
    , config_{ config }
{
    // Older config files have no spam settings yet; bring them into range.
    auto& features{ config_.get_feature_config() };
    features.spam_interval_s = std::clamp(features.spam_interval_s, MIN_INTERVAL_S, MAX_INTERVAL_S);
    features.spam_text = sanitize(features.spam_text);

    console_handle_ = event::ScopedHandle{
        dispatcher_,
        console_message_type,
        dispatcher_.append_listener(console_message_type, [this](const event::Event& e) { on_server_message(e); })
    };
    input_handle_ = event::ScopedHandle{
        dispatcher_,
        input_type,
        dispatcher_.append_listener(input_type, [this](const event::Event& e) { on_player_input(e); })
    };
    spawn_handle_ = event::ScopedHandle{
        dispatcher_,
        spawn_type,
        dispatcher_.append_listener(spawn_type, [this](const event::Event& e) { on_spawn(e); })
    };
}

SpamHandler::~SpamHandler()
{
    stop();
}

std::string SpamHandler::sanitize(const std::string_view text)
{
    return utils::sanitize_chat_message(text, MAX_LENGTH);
}

void SpamHandler::set_enabled(bool enabled)
{
    if (enabled && get_text().empty()) {
        send_log("`4Once bir mesaj yaz: ``/spam`` menusunden ya da ``/spam yazi <mesaj>");
        enabled = false;
    }

    // The server would run it as a command every few seconds (warps, paid
    // broadcasts...), which is never what an auto message is for.
    if (enabled && get_text().starts_with('/')) {
        send_log("`4Komutlar spamlanamaz: ``mesaj / ile baslamasin.");
        enabled = false;
    }

    enabled_ = enabled;
    if (enabled_) {
        start();
    }
    else {
        stop();
    }
}

const std::string& SpamHandler::set_text(const std::string_view text)
{
    config_.get_feature_config().spam_text = sanitize(text);
    std::ignore = config_.save();

    if (enabled_ && (get_text().empty() || get_text().starts_with('/'))) {
        set_enabled(false);
        send_log("`4Spam durduruldu: ``yeni mesaj bos ya da bir komut.");
    }

    return get_text();
}

int SpamHandler::set_interval(const int seconds)
{
    const int interval{ std::clamp(seconds, MIN_INTERVAL_S, MAX_INTERVAL_S) };
    if (interval == get_interval()) {
        return interval;
    }

    config_.get_feature_config().spam_interval_s = interval;
    std::ignore = config_.save();

    // Restart so the new interval takes effect now, not after the old one.
    if (enabled_) {
        stop();
        start();
    }

    return interval;
}

void SpamHandler::start()
{
    if (task_id_ != INVALID_TASK_ID) {
        return;
    }

    // Scheduler callbacks run on a worker thread; ENet is not thread-safe, so
    // hop back to the main thread before sending the packet.
    Scheduler* const scheduler{ scheduler_.get() };
    task_id_ = scheduler_->schedule_periodic(
        [this, scheduler] { scheduler->post_to_main_thread([this] { tick(); }); },
        std::chrono::seconds{ get_interval() },
        TASK_TAG,
        TaskPriority::Normal,
        std::chrono::milliseconds{ 500 }
    );
}

void SpamHandler::stop()
{
    if (task_id_ == INVALID_TASK_ID) {
        return;
    }

    scheduler_->cancel_by_tag(TASK_TAG);
    task_id_ = INVALID_TASK_ID;
}

void SpamHandler::tick()
{
    if (!enabled_ || get_text().empty() || !client_.is_connected()) {
        return;
    }

    // Chat only reaches anyone inside a world; outside one the server would
    // just ignore it, so wait until the player joins one.
    if (world::World::instance().get_local_net_id() < 0) {
        return;
    }

    // The server counts every line the player says, so wait a full interval
    // after the last one (ours, typed by hand, or arriving in a world). The
    // slack keeps timer jitter from skipping a whole period.
    const auto now{ std::chrono::steady_clock::now() };
    if (now - last_sent_ < std::chrono::seconds{ get_interval() } - std::chrono::milliseconds{ 750 }) {
        return;
    }

    // Exactly what the client sends when you chat; message::Input would drop
    // the line break before "|text|".
    if (client_.write(packet::message::make_raw_text(packet::NET_MESSAGE_GENERIC_TEXT, "action|input\n|text|" + get_text()))) {
        last_sent_ = now;
    }
}

void SpamHandler::on_player_input(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::Input>*>(&event) };
    if (evt && evt->direction == event::Direction::ServerBound && !evt->canceled) {
        last_sent_ = std::chrono::steady_clock::now();
    }
}

void SpamHandler::on_spawn(const event::Event& event)
{
    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::OnSpawn>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    // Entered a world: nothing goes out the moment the player arrives.
    if (const auto pkt{ evt->get<packet::game::OnSpawn>() }; pkt && pkt->type == "local") {
        last_sent_ = std::chrono::steady_clock::now();
    }
}

void SpamHandler::on_server_message(const event::Event& event)
{
    if (!enabled_) {
        return;
    }

    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::OnConsoleMessage>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::OnConsoleMessage>() };
    if (!pkt) {
        return;
    }

    // Only the server's own notices count, never anything a player can send
    // (chat, bubbles, private messages), so nobody can switch the spam off.
    if (utils::game_text::is_player_chat(pkt->text) || pkt->text.find("player_chat=") != std::string::npos) {
        return;
    }

    // Stop as soon as the server complains or mutes us, before it gets worse.
    const auto text{ lowercase(utils::strip_color_codes(pkt->text)) };
    constexpr std::array STOP_SIGNS{
        std::string_view{ "spam detected" },
        std::string_view{ "duct tape has covered your mouth" },
        std::string_view{ "cursed to (the world)" },
        std::string_view{ "reality flickers" },
    };
    if (std::ranges::any_of(STOP_SIGNS, [&text](const std::string_view sign) { return text.find(sign) != std::string::npos; })) {
        set_enabled(false);
        spdlog::warn("Spam stopped: the server reported spam");
        send_log("`4Sunucu spam uyarisi verdi, spam durduruldu. ``Araligi artir.");
    }
}

void SpamHandler::send_log(const std::string& msg) const
{
    packet::message::Log log_pkt{};
    log_pkt.msg = msg;
    std::ignore = packet::PacketHelper::write(log_pkt, server_);
}
}
