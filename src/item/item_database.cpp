#include "item_database.hpp"

#include <spdlog/spdlog.h>
#include <fstream>

#include "../utils/hash.hpp"

namespace item {
namespace {
const std::string_view DECRYPT_KEY = "PBG892FXX982ABC*";
}

std::string ItemDatabase::decrypt_item_name(std::string_view encrypted, std::uint32_t item_id)
{
    std::string decrypted;
    decrypted.reserve(encrypted.size());

    for (std::size_t i = 0; i < encrypted.size(); ++i) {
        const std::size_t key_index = (i + item_id) % DECRYPT_KEY.size();
        decrypted += static_cast<char>(encrypted[i] ^ DECRYPT_KEY[key_index]);
    }

    return decrypted;
}

std::optional<ItemInfo> ItemDatabase::parse_item(utils::ByteStream<>& bs, const std::uint16_t version)
{
    ItemInfo item{};
    bool ok{ true };

    ok &= bs.read(item.item_id);
    ok &= bs.read(item.flags);
    ok &= bs.read(item.item_type);
    ok &= bs.read(item.material);

    ok &= bs.read(item.item_name);
    item.item_name = decrypt_item_name(item.item_name, item.item_id);

    ok &= bs.read(item.texture_file_path);
    ok &= bs.read(item.texture_file_hash);

    ok &= bs.read(item.visual_effect);
    ok &= bs.read(item.cooking_time);

    ok &= bs.read(item.texture_coord_x);
    ok &= bs.read(item.texture_coord_y);

    ok &= bs.read(item.texture_type);

    ok &= bs.read(item.is_stripey_wallpaper);

    ok &= bs.read(item.collision_type);

    ok &= bs.read(item.health);
    ok &= bs.read(item.reset_time);

    ok &= bs.read(item.clothing_type);

    ok &= bs.read(item.rarity);
    ok &= bs.read(item.max_amount);

    ok &= bs.read(item.extra_file);
    ok &= bs.read(item.extra_file_hash);

    ok &= bs.read(item.animation_time);

    ok &= bs.read(item.pet_name);
    ok &= bs.read(item.pet_prefix);
    ok &= bs.read(item.pet_suffix);
    ok &= bs.read(item.pet_ability);

    ok &= bs.read(item.seed_base);
    ok &= bs.read(item.seed_overlay);
    ok &= bs.read(item.tree_base);
    ok &= bs.read(item.tree_leaves);
    ok &= bs.read(item.seed_color);
    ok &= bs.read(item.seed_overlay_color);
    ok &= bs.read(item.ingredient);
    ok &= bs.read(item.grow_time);

    ok &= bs.read(item.fx_flag);

    ok &= bs.read(item.animating_coordinates);
    ok &= bs.read(item.animating_texture_file);
    ok &= bs.read(item.animating_coordinates_2);

    ok &= bs.read(item.__unk_1);
    ok &= bs.read(item.__unk_2);

    ok &= bs.read(item.flags_2);

    ok &= bs.read(item.__unk_3);

    ok &= bs.read(item.tile_range);

    ok &= bs.read(item.vault_capacity);

    ok &= bs.read(item.punch_options);

    ok &= bs.read(item.__unk_4);

    ok &= bs.read(item.body_part_list);

    ok &= bs.read(item.light_range);

    ok &= bs.read(item.__unk_5);

    ok &= bs.read(item.can_sit);
    ok &= bs.read(item.player_offset_x);
    ok &= bs.read(item.player_offset_y);
    ok &= bs.read(item.chair_texture_x);
    ok &= bs.read(item.chair_texture_y);
    ok &= bs.read(item.chair_leg_offset_x);
    ok &= bs.read(item.chair_leg_offset_y);
    ok &= bs.read(item.chair_texture_file);
    ok &= bs.read(item.renderer_data_file);

    ok &= bs.read(item.__unk_6);
    ok &= bs.read(item.renderer_data_file_hash);

    ok &= bs.read(item.__unk_7);
    ok &= bs.read(item.__unk_8);

    ok &= bs.read(item.info);

    ok &= bs.read(item.ingredients);

    if (version >= 24) {
        ok &= bs.read(item.__unk_9);
    }

    if (version >= 25) {
        ok &= bs.read(item.hit_sound_fx);
        ok &= bs.read(item.hit_sound_fx_hash);
    }

    if (version >= 26) {
        ok &= bs.read(item.__unk_10);
    }

    if (version >= 27) {
        ok &= bs.read(item.__unk_11);
    }

    if (!ok) {
        return std::nullopt;
    }

    return item;
}

bool ItemDatabase::load_from_file(const std::string& file_path)
{
    std::ifstream file{ file_path, std::ios::binary | std::ios::ate };
    if (!file) {
        spdlog::error("Failed to open items.dat: {}", file_path);
        return false;
    }

    const auto file_size{ file.tellg() };
    if (file_size <= 0) {
        spdlog::error("items.dat is empty or unreadable: {}", file_path);
        return false;
    }

    file.seekg(0, std::ios::beg);

    std::vector<std::byte> buffer(file_size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        spdlog::error("Failed to read items.dat: {}", file_path);
        return false;
    }

    return parse(buffer);
}

bool ItemDatabase::parse(std::span<const std::byte> data)
{
    utils::ByteStream<> bs{ data };

    std::uint16_t version{ 0 };
    std::uint32_t count{ 0 };
    if (!bs.read(version) || !bs.read(count)) {
        spdlog::error("Failed to read items.dat header");
        return false;
    }

    spdlog::info("Parsing items.dat: version={}, count={}", version, count);

    if (version > MAX_SUPPORTED_VERSION) {
        spdlog::warn("items.dat version {} is newer than the newest supported ({}), trying anyway", version, MAX_SUPPORTED_VERSION);
    }

    // Every item record is well over this size, so a count the data cannot
    // hold is corrupt; checked before reserving memory for it.
    constexpr std::size_t min_item_size{ 200 };
    if (count > bs.remaining() / min_item_size) {
        spdlog::error("items.dat claims {} items but has only {} bytes of data", count, bs.remaining());
        return false;
    }

    // Parse into a local list and only swap it in once the whole file was
    // understood, so a newer/unknown layout keeps the last good database
    // instead of leaving a half-parsed one with garbage item ids.
    std::vector<ItemInfo> items{};
    items.reserve(count);

    for (std::uint32_t i{ 0 }; i < count; ++i) {
        auto item{ parse_item(bs, version) };
        if (!item.has_value()) {
            spdlog::error("items.dat (version {}) ended early at item {} of {}", version, i, count);
            return false;
        }

        if (item->item_id != i) {
            spdlog::error(
                "items.dat (version {}) is out of sync at item {} (read id {}); this layout is not supported",
                version,
                i,
                item->item_id
            );
            return false;
        }

        items.push_back(std::move(*item));
    }

    if (bs.remaining() != 0) {
        spdlog::error(
            "items.dat (version {}) has {} unread bytes after the last item; this layout is not supported",
            version,
            bs.remaining()
        );
        return false;
    }

    version_ = version;
    count_ = count;
    hash_ = utils::hash::proton(reinterpret_cast<const char*>(data.data()), data.size());
    items_ = std::move(items);

    spdlog::info("Successfully parsed {} items from items.dat", items_.size());
    return true;
}

const ItemInfo* ItemDatabase::get_item(std::uint32_t id) const
{
    if (id >= items_.size()) {
        return nullptr;
    }

    return &items_[id];
}

void ItemDatabase::clear()
{
    version_ = 0;
    count_ = 0;
    hash_ = 0;
    items_.clear();
}
}
