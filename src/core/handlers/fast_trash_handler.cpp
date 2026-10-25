#include "fast_trash_handler.hpp"

#include <charconv>
#include <string>
#include <tuple>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../../item/item_database.hpp"
#include "../../item/locks.hpp"
#include "../../packet/game/dialog.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/message/raw_text.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../player/inventory.hpp"
#include "../../utils/game_text.hpp"

namespace core::handlers {
namespace {
    constexpr auto dialog_event_type{ event::packet_event_type(packet::PacketId::OnDialogRequest) };

    // Every kind of lock always asks (the ids cover a missing items.dat).
    [[nodiscard]] bool is_protected(const int item_id)
    {
        if (item_id == item::locks::WORLD_LOCK || item_id == item::locks::DIAMOND_LOCK || item_id == item::locks::BLUE_GEM_LOCK) {
            return true;
        }
        const auto* item{ item::ItemDatabase::instance().get_item(static_cast<std::uint32_t>(item_id)) };
        return item && item->item_type == item::ItemType::Lock;
    }
}

FastTrashHandler::FastTrashHandler(event::Dispatcher& dispatcher, network::Client& client, network::Server& server, LegitManager& legit, Config& config)
    : client_{ client }
    , server_{ server }
    , legit_{ legit }
    , config_{ config }
{
    handle_ = event::ScopedHandle{
        dispatcher,
        dialog_event_type,
        dispatcher.append_listener(dialog_event_type, [this](const event::Event& e) { on_dialog(e); })
    };
}

void FastTrashHandler::set_enabled(const bool enabled)
{
    config_.get_feature_config().fast_trash = enabled;
    std::ignore = config_.save();
}

void FastTrashHandler::on_dialog(const event::Event& event)
{
    if (!is_enabled()) {
        return;
    }

    const auto* evt{ dynamic_cast<const event::TypedPacketEvent<packet::PacketId::OnDialogRequest>*>(&event) };
    if (!evt || evt->direction != event::Direction::ClientBound) {
        return;
    }

    const auto pkt{ evt->get<packet::game::OnDialogRequest>() };
    if (!pkt) {
        return;
    }

    // The same dialog for TRASH and RECYCLE; only its title differs.
    const auto dialog{ packet::game::parse_dialog(pkt->dialog) };
    if (dialog.name != "trash_item") {
        return;
    }

    int item_id{ 0 };
    if (const auto embed{ dialog.get_embed_data("itemID") }) {
        std::ignore = std::from_chars(embed->data(), embed->data() + embed->size(), item_id);
    }
    if (item_id <= 0 || is_protected(item_id)) {
        return; // Let the player decide.
    }

    // Unlike the drop dialog, the count box starts at 0; the amount is in the text.
    int owned{ utils::game_text::trash_owned_amount(pkt->dialog) };
    if (owned <= 0 && player::Inventory::instance().is_known()) {
        owned = player::Inventory::instance().get(static_cast<std::uint16_t>(item_id));
    }
    if (owned <= 0) {
        return;
    }

    if (!client_.is_connected()) {
        return;
    }

    // Hide the dialog now, answer on our turn.
    evt->cancel();
    legit_.run([this, reply = packet::game::make_dialog_return(dialog, { { "count", std::to_string(owned) } }), owned, item_id] {
        if (!client_.write(packet::message::make_raw_text(packet::NET_MESSAGE_GENERIC_TEXT, reply))) {
            return;
        }

        spdlog::info("Fast trash: trashed {} of item {}", owned, item_id);

        const auto* item{ item::ItemDatabase::instance().get_item(static_cast<std::uint32_t>(item_id)) };
        packet::message::Log log{};
        log.msg = fmt::format("`9[Trash]`` `w{}`` x{}", item ? item->item_name : std::to_string(item_id), owned);
        std::ignore = packet::PacketHelper::write(log, server_);
    });
}
}
