#include "roulette_handler.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <tuple>
#include <type_traits>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../../packet/game/message.hpp"
#include "../../world/player_finder.hpp"
#include "../../world/world.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../utils/roulette.hpp"

namespace core::handlers {
RouletteHandler::RouletteHandler(event::Dispatcher& dispatcher, network::Server& server, Config& config)
    : dispatcher_{ dispatcher }
    , server_{ server }
    , config_{ config }
    , enabled_{ config.get_feature_config().roulette }
    , mode_{ mode_from_string(config.get_feature_config().roulette_mode) }
{
    listen<packet::PacketId::OnTalkBubble, packet::game::OnTalkBubble>();
    listen<packet::PacketId::OnConsoleMessage, packet::game::OnConsoleMessage>();

    // Spin history is per world.
    constexpr auto join_type{ event::packet_event_type(packet::PacketId::JoinRequest) };
    handles_.emplace_back(
        dispatcher_,
        join_type,
        dispatcher_.append_listener(join_type, [this](const event::Event& event) {
            const auto evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::JoinRequest>*>(&event) };
            if (evt && evt->direction == event::Direction::ServerBound) {
                history_.clear();
            }
        })
    );
}

void RouletteHandler::set_enabled(const bool enabled)
{
    enabled_ = enabled;

    config_.get_feature_config().roulette = enabled;
    std::ignore = config_.save();
}

void RouletteHandler::set_mode(const Mode mode)
{
    mode_ = mode;

    config_.get_feature_config().roulette_mode = std::string{ mode_to_string(mode) };
    std::ignore = config_.save();
}

void RouletteHandler::set_mode_from_string(const std::string_view name)
{
    set_mode(mode_from_string(name));
}

std::string_view RouletteHandler::mode_to_string(const Mode mode)
{
    switch (mode) {
    case Mode::Real: return "real";
    case Mode::Reme: return "reme";
    case Mode::Qq:   return "qq";
    }

    return "reme";
}

RouletteHandler::Mode RouletteHandler::mode_from_string(const std::string_view name)
{
    std::string n{ name };
    std::ranges::transform(n, n.begin(), [](const unsigned char c) { return std::tolower(c); });

    if (n == "real") {
        return Mode::Real;
    }

    if (n == "qq") {
        return Mode::Qq;
    }

    return Mode::Reme;
}

template <packet::PacketId Id, typename Packet>
void RouletteHandler::listen()
{
    constexpr auto type{ event::packet_event_type(Id) };
    handles_.emplace_back(
        dispatcher_,
        type,
        dispatcher_.append_listener(type, [this](const event::Event& event) {
            const auto evt{ dynamic_cast<const event::TypedPacketEvent<Id>*>(&event) };
            if (!evt || evt->direction != event::Direction::ClientBound) {
                return;
            }

            if (const auto pkt{ evt->template get<Packet>() }) {
                std::int32_t net_id{ -1 };
                if constexpr (std::is_same_v<Packet, packet::game::OnTalkBubble>) {
                    net_id = pkt->net_id;
                }
                on_message(pkt->text, packet::game::variant_delay(pkt->game_packet), net_id);
            }
        })
    );
}

std::int32_t RouletteHandler::find_spinner(const std::string_view spinner, const std::int32_t net_id_hint)
{
    const auto& world{ world::World::instance() };
    if (net_id_hint >= 0 && !world.get_player(net_id_hint).expired()) {
        return net_id_hint;
    }

    // The console message only names the spinner; only an exact name counts.
    const auto wanted{ world::normalize_player_name(spinner) };
    for (const auto& match : world::find_players_by_name(world.get_players(), spinner, -1)) {
        if (world::normalize_player_name(match.name) == wanted) {
            return match.net_id;
        }
    }

    return net_id_hint;
}

void RouletteHandler::on_message(const std::string_view text, const std::int32_t delay, const std::int32_t spinner_net_id)
{
    const auto spin{ utils::roulette::parse_spin_message(text) };
    if (!spin) {
        return;
    }

    spdlog::info("Roulette: {} spun {} (display delay {} ms)", spin->spinner, spin->number, delay);

    if (delay <= 0) {
        return;
    }

    const auto now{ std::chrono::steady_clock::now() };
    auto key{ fmt::format("{}|{}", spin->spinner, spin->number) };
    if (key == last_spin_key_ && now - last_spin_time_ < DUPLICATE_WINDOW) {
        return;
    }

    last_spin_key_ = std::move(key);
    last_spin_time_ = now;

    history_.push_front(SpinRecord{ spin->spinner, spin->number, spin->color_code, now });
    if (history_.size() > MAX_HISTORY) {
        history_.pop_back();
    }

    if (!enabled_) {
        return;
    }

    const auto number{ fmt::format("{}{}``", spin->color_code, spin->number) };

    int mode_value{ spin->number };
    switch (mode_) {
    case Mode::Real: mode_value = spin->number; break;
    case Mode::Reme: mode_value = utils::roulette::reme(spin->number); break;
    case Mode::Qq:   mode_value = spin->number % 10; break;
    }
    const auto mode_label{ mode_to_string(mode_) };

    packet::message::Log log{};
    log.msg = fmt::format("`9[Spin] ```w{}`` got {} `o({}={}, {:.1f}s early)``", spin->spinner, number, mode_label, mode_value, delay / 1000.0);
    send_to_player(log);

    // Above the spinner's head like a normal chat bubble; the server's own
    // bubble replaces it once the wheel stops. Without a known player, fall
    // back to the overlay at the top of the screen.
    if (const auto net_id{ find_spinner(spin->spinner, spinner_net_id) }; net_id >= 0) {
        packet::game::OnTalkBubble bubble{};
        bubble.net_id = net_id;
        bubble.text = fmt::format("`9[Spin]`` {} `o({}={})``", number, mode_label, mode_value);
        send_to_player(bubble);
        return;
    }

    packet::game::OnTextOverlay overlay{};
    overlay.text = fmt::format("`w{}`` got {} `o({}={})``", spin->spinner, number, mode_label, mode_value);
    send_to_player(overlay);
}

void RouletteHandler::send_to_player(packet::IPacket& packet) const
{
    std::ignore = packet::PacketHelper::write(packet, server_);
}
}
