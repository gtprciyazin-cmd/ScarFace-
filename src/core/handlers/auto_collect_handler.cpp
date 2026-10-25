#include "auto_collect_handler.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "../../packet/game/world.hpp"
#include "../../packet/packet_helper.hpp"
#include "../../world/object_finder.hpp"
#include "../../world/world.hpp"

namespace core::handlers {
AutoCollectHandler::AutoCollectHandler(
    event::Dispatcher& dispatcher,
    network::Client& client,
    std::shared_ptr<Scheduler> scheduler,
    LegitManager& legit,
    Config& config
)
    : dispatcher_{ dispatcher }
    , client_{ client }
    , scheduler_{ std::move(scheduler) }
    , legit_{ legit }
    , config_{ config }
    , enabled_{ config.get_feature_config().auto_collect }
    , range_{ std::clamp(config.get_feature_config().auto_collect_range, MIN_RANGE, MAX_RANGE) }
    , delay_ms_{ std::clamp(config.get_feature_config().auto_collect_delay_ms, MIN_DELAY_MS, MAX_DELAY_MS) }
{
    setup_handlers();
}

void AutoCollectHandler::set_enabled(const bool enabled)
{
    enabled_ = enabled;
    attempts_.clear();

    config_.get_feature_config().auto_collect = enabled;
    std::ignore = config_.save();
}

float AutoCollectHandler::set_range(const float range_tiles)
{
    range_ = std::clamp(range_tiles, MIN_RANGE, MAX_RANGE);

    config_.get_feature_config().auto_collect_range = range_;
    std::ignore = config_.save();
    return range_;
}

int AutoCollectHandler::set_delay_ms(const int delay_ms)
{
    delay_ms_ = std::clamp(delay_ms, MIN_DELAY_MS, MAX_DELAY_MS);

    config_.get_feature_config().auto_collect_delay_ms = delay_ms_;
    std::ignore = config_.save();
    return delay_ms_;
}

void AutoCollectHandler::schedule_pass(const std::chrono::milliseconds wait)
{
    if (pass_scheduled_) {
        return;
    }

    pass_scheduled_ = true;
    // Scheduler callbacks run on a worker thread; hop back to the main thread.
    const auto scheduler{ scheduler_.get() };
    scheduler_->schedule_delayed(
        [this, scheduler] {
            scheduler->post_to_main_thread([this] {
                pass_scheduled_ = false;
                std::ignore = collect_nearby();
            });
        },
        wait,
        "autocollect"
    );
}

std::size_t AutoCollectHandler::collect_nearby()
{
    if (!enabled_ || !client_.is_connected()) {
        return 0;
    }

    auto& world{ world::World::instance() };
    const auto local_player{ world.get_local_player().lock() };
    if (!local_player) {
        return 0;
    }

    const auto& objects{ world.get_object_map().get_objects() };

    // Forget objects that are gone (collected, despawned, or a new world).
    const auto is_gone = [&objects](const std::uint32_t id) {
        return std::ranges::none_of(objects, [id](const world::Object& obj) { return obj.object_id == id; });
    };
    std::erase_if(attempts_, [&is_gone](const auto& entry) { return is_gone(entry.first); });
    std::erase_if(ignored_objects_, is_gone);

    const glm::vec2 position{ local_player->position() };
    const auto now{ std::chrono::steady_clock::now() };

    // With a delay, one request per pass and never sooner than the delay; a
    // timer comes back for whatever is still in range.
    const std::chrono::milliseconds delay{ delay_ms_ };
    const std::size_t max_requests{ delay_ms_ > 0 ? 1 : MAX_REQUESTS_PER_PASS };

    std::size_t sent{ 0 };
    for (const auto* object : world::find_objects_in_range(objects, position, range_)) {
        if (sent >= max_requests) {
            if (delay_ms_ > 0) {
                schedule_pass(delay);
            }
            break;
        }

        if (delay_ms_ > 0 && now - last_request_ < delay) {
            schedule_pass(std::chrono::ceil<std::chrono::milliseconds>(delay - (now - last_request_)));
            break;
        }

        if (ignored_objects_.contains(object->object_id)) {
            continue;
        }

        auto& attempt{ attempts_[object->object_id] };
        if (attempt.count >= MAX_ATTEMPTS) {
            continue;
        }

        if (attempt.count > 0 && now - attempt.last_sent < RETRY_DELAY) {
            continue;
        }

        // Legit mode: one pickup per turn; the timer comes back for the rest.
        if (!legit_.try_acquire()) {
            schedule_pass(std::max(legit_.ready_in(), LEGIT_RETRY));
            break;
        }

        packet::game::ItemActivateObjectRequest request{};
        request.net_id = local_player->net_id();
        request.pos_x = object->pos.x;
        request.pos_y = object->pos.y;
        request.object_id = object->object_id;

        if (!packet::PacketHelper::write(request, client_)) {
            spdlog::warn("Auto collect: failed to send pickup request for object {}", object->object_id);
            break;
        }

        ++attempt.count;
        attempt.last_sent = now;
        last_request_ = now;
        ++sent;

        spdlog::debug(
            "Auto collect: requested object {} (item {} x{}) at ({:.0f}, {:.0f}), attempt {}",
            object->object_id,
            object->item_id,
            object->amount,
            object->pos.x,
            object->pos.y,
            attempt.count
        );
    }

    return sent;
}

void AutoCollectHandler::ignore_drops_of(const std::uint16_t item_id, const std::chrono::milliseconds window)
{
    ignore_until_[item_id] = std::chrono::steady_clock::now() + window;
}

void AutoCollectHandler::track_new_drop(const std::uint16_t item_id)
{
    const auto it{ ignore_until_.find(item_id) };
    if (it == ignore_until_.end()) {
        return;
    }

    if (std::chrono::steady_clock::now() > it->second) {
        ignore_until_.erase(it);
        return;
    }

    // The WorldHandler has already added the drop, which got the newest id.
    ignored_objects_.insert(world::World::instance().get_object_map().get_drop_id());
}

void AutoCollectHandler::reset()
{
    attempts_.clear();
    ignore_until_.clear();
    ignored_objects_.clear();
}

template <packet::PacketId Id, typename Func>
void AutoCollectHandler::listen(const event::Direction direction, Func&& func)
{
    constexpr auto type{ event::packet_event_type(Id) };
    handles_.emplace_back(
        dispatcher_,
        type,
        // Lower priority than the WorldHandler so the world state is already updated.
        dispatcher_.append_listener(type, [direction, func = std::forward<Func>(func)](const event::Event& event) {
            const auto evt{ dynamic_cast<const event::TypedPacketEvent<Id>*>(&event) };
            if (!evt || evt->direction != direction) {
                return;
            }

            func(*evt);
        }, event::Priority::FairlyLow)
    );
}

void AutoCollectHandler::setup_handlers()
{
    // The player moved, something was dropped, or the player (re)spawned.
    listen<packet::PacketId::State>(event::Direction::ServerBound, [this](const auto&) { std::ignore = collect_nearby(); });
    listen<packet::PacketId::OnSpawn>(event::Direction::ClientBound, [this](const auto&) { std::ignore = collect_nearby(); });
    listen<packet::PacketId::ItemChangeObject>(event::Direction::ClientBound, [this](const auto& evt) {
        if (const auto pkt{ evt.template get<packet::game::ItemChangeObject>() }; pkt && pkt->object_change_type == -1) {
            track_new_drop(static_cast<std::uint16_t>(pkt->item_id));
        }

        std::ignore = collect_nearby();
    });

    // Object ids restart in every world.
    listen<packet::PacketId::JoinRequest>(event::Direction::ServerBound, [this](const auto&) { reset(); });
    listen<packet::PacketId::SendMapData>(event::Direction::ClientBound, [this](const auto&) { reset(); });
}
}
