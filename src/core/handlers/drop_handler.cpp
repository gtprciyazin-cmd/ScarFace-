#include "drop_handler.hpp"

#include <array>
#include <charconv>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "../../item/lock_conversion.hpp"
#include "../../packet/game/inventory.hpp"
#include "../../packet/message/chat.hpp"
#include "../../packet/message/raw_text.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
namespace {
template <typename T>
std::optional<T> parse_number(const std::string_view text)
{
    T value{};
    if (auto [ptr, ec]{ std::from_chars(text.data(), text.data() + text.size(), value) }; ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }

    return value;
}
}

DropHandler::DropHandler(
    network::Client& client,
    network::Server& server,
    DialogInterceptor& dialogs,
    AutoCollectHandler& auto_collect
)
    : client_{ client }
    , server_{ server }
    , dialogs_{ dialogs }
    , auto_collect_{ auto_collect }
{

}

DropHandler::Result DropHandler::request_drop(const std::uint16_t item_id, const int amount, const std::string& item_name)
{
    if (amount < 1 || amount > MAX_AMOUNT) {
        return Result::InvalidAmount;
    }

    if (!client_.is_connected()) {
        return Result::NotConnected;
    }

    if (world::World::instance().get_local_net_id() < 0) {
        return Result::NotInWorld;
    }

    auto& inventory{ player::Inventory::instance() };
    const auto plan{ item::locks::plan_conversion(item_id, amount, inventory) };
    if (!plan) {
        start_drop(item_id, amount, item_name);
        return Result::Sent;
    }

    for (int i{ 0 }; i < plan->count; ++i) {
        packet::game::ItemActivateRequest activate{};
        activate.item_id = plan->source_item;
        if (!packet::PacketHelper::write(activate, client_)) {
            return Result::NotConnected;
        }
    }

    // Assume it worked; the drop dialog reports the real count and corrects it.
    inventory.add(plan->source_item, -plan->count);
    inventory.add(item_id, plan->count * item::locks::TIER_RATIO);

    spdlog::info("Drop: shattering {} x item {} before dropping {} x item {}", plan->count, plan->source_item, amount, item_id);
    dialogs_.run_later(CONVERSION_DELAY, [this, item_id, amount, item_name] { start_drop(item_id, amount, item_name); });
    return Result::Converting;
}

void DropHandler::start_drop(const std::uint16_t item_id, const int amount, const std::string& item_name)
{
    if (!send_text(fmt::format("action|drop\n|itemID|{}", item_id))) {
        send_log(fmt::format("`4Drop failed: ``lost connection while dropping {}", item_name));
        return;
    }

    dialogs_.expect(
        "drop_item",
        [this, item_id, amount, item_name](const packet::game::DialogInfo& dialog) {
            return on_drop_dialog(dialog, item_id, amount, item_name);
        },
        DIALOG_TIMEOUT,
        [this, item_name] {
            send_log(fmt::format("`4Drop failed: ``the server did not ask how many {} to drop. Do you have any?", item_name));
        }
    );

    spdlog::info("Drop: requested drop of {} x item {}", amount, item_id);
}

bool DropHandler::on_drop_dialog(
    const packet::game::DialogInfo& dialog,
    const std::uint16_t item_id,
    const int amount,
    const std::string& item_name
) {
    const auto dialog_item{ dialog.get_embed_data("itemID") };
    if (!dialog_item || parse_number<std::uint16_t>(*dialog_item) != item_id) {
        // Another item's drop dialog, e.g. one the player opened by hand.
        return false;
    }

    auto& inventory{ player::Inventory::instance() };

    // The amount box defaults to everything the player holds: the real count.
    if (const auto input{ dialog.get_text_input("count") }) {
        if (const auto owned{ parse_number<int>(*input) }) {
            inventory.set(item_id, *owned);
            if (*owned < amount) {
                send_log(fmt::format("`4Not enough: ``you have `w{}`` {}, not dropping {}", *owned, item_name, amount));
                return true;
            }
        }
    }

    auto_collect_.ignore_drops_of(item_id, AUTO_COLLECT_IGNORE_WINDOW);

    const auto reply{ packet::game::make_dialog_return(dialog, { { "count", std::to_string(amount) } }) };
    if (!send_text(reply)) {
        send_log(fmt::format("`4Drop failed: ``lost connection while dropping {}", item_name));
        return true;
    }

    inventory.add(item_id, -amount);
    spdlog::info("Drop: answered drop dialog for item {} with count {}", item_id, amount);
    return true;
}

DropHandler::Result DropHandler::request_drop_all_locks()
{
    if (!client_.is_connected()) {
        return Result::NotConnected;
    }

    if (world::World::instance().get_local_net_id() < 0) {
        return Result::NotInWorld;
    }

    drop_all_step(0, std::make_shared<std::vector<std::string>>());
    return Result::Sent;
}

void DropHandler::drop_all_step(const std::size_t index, std::shared_ptr<std::vector<std::string>> dropped)
{
    struct LockType {
        std::uint16_t item_id;
        std::string_view name;
    };
    static constexpr std::array<LockType, 3> LOCKS{ {
        { item::locks::WORLD_LOCK, "World Lock" },
        { item::locks::DIAMOND_LOCK, "Diamond Lock" },
        { item::locks::BLUE_GEM_LOCK, "Blue Gem Lock" },
    } };

    if (index >= LOCKS.size()) {
        if (dropped->empty()) {
            send_log("`4Drop all: ``you have no World, Diamond or Blue Gem Locks");
            return;
        }

        std::string summary{};
        for (const auto& part : *dropped) {
            summary += summary.empty() ? part : ", " + part;
        }
        send_log(fmt::format("`2Dropped ``{}", summary));
        return;
    }

    const auto item_id{ LOCKS[index].item_id };
    const std::string name{ LOCKS[index].name };
    const auto next{ [this, index, dropped] {
        dialogs_.run_later(DROP_ALL_STEP_DELAY, [this, index, dropped] { drop_all_step(index + 1, dropped); });
    } };

    // Skip lock types the inventory says the player has none of; when the
    // inventory is unknown, the server's dialog (or its absence) decides.
    if (const auto& inventory{ player::Inventory::instance() }; inventory.is_known() && inventory.get(item_id) <= 0) {
        drop_all_step(index + 1, dropped);
        return;
    }

    if (!send_text(fmt::format("action|drop\n|itemID|{}", item_id))) {
        send_log(fmt::format("`4Drop failed: ``lost connection while dropping {}", name));
        return;
    }

    dialogs_.expect(
        "drop_item",
        [this, item_id, name, dropped, next](const packet::game::DialogInfo& dialog) {
            const auto dialog_item{ dialog.get_embed_data("itemID") };
            if (!dialog_item || parse_number<std::uint16_t>(*dialog_item) != item_id) {
                return false;
            }

            // The amount box defaults to everything the player holds.
            int owned{ 0 };
            if (const auto input{ dialog.get_text_input("count") }) {
                owned = parse_number<int>(*input).value_or(0);
            }

            if (owned > 0) {
                auto_collect_.ignore_drops_of(item_id, AUTO_COLLECT_IGNORE_WINDOW);
                const auto reply{ packet::game::make_dialog_return(dialog, { { "count", std::to_string(owned) } }) };
                if (!send_text(reply)) {
                    send_log(fmt::format("`4Drop failed: ``lost connection while dropping {}", name));
                    return true;
                }

                player::Inventory::instance().set(item_id, 0);
                dropped->push_back(fmt::format("`w{}`` {}", owned, name));
                spdlog::info("Drop all: dropped {} x item {}", owned, item_id);
            }

            next();
            return true;
        },
        DIALOG_TIMEOUT,
        // No dialog: the player has none of this lock.
        next
    );
}

bool DropHandler::send_text(const std::string_view text) const
{
    return client_.write(packet::message::make_raw_text(packet::NET_MESSAGE_GENERIC_TEXT, text));
}

void DropHandler::send_log(const std::string& msg) const
{
    packet::message::Log log_pkt{};
    log_pkt.msg = msg;
    std::ignore = packet::PacketHelper::write(log_pkt, server_);
}
}
