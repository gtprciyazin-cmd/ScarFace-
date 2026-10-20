#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "item/item_database.hpp"
#include "utils/hash.hpp"

using namespace item;

namespace {
// Writes one item record in the order parse_item() reads it, with every field
// zero/empty except the id.
void write_item(utils::ByteStream<>& bs, const std::uint32_t id, const std::uint16_t version)
{
    ItemInfo item{};
    item.item_id = id;

    bs.write(item.item_id);
    bs.write(item.flags);
    bs.write(item.item_type);
    bs.write(item.material);
    bs.write(item.item_name);
    bs.write(item.texture_file_path);
    bs.write(item.texture_file_hash);
    bs.write(item.visual_effect);
    bs.write(item.cooking_time);
    bs.write(item.texture_coord_x);
    bs.write(item.texture_coord_y);
    bs.write(item.texture_type);
    bs.write(item.is_stripey_wallpaper);
    bs.write(item.collision_type);
    bs.write(item.health);
    bs.write(item.reset_time);
    bs.write(item.clothing_type);
    bs.write(item.rarity);
    bs.write(item.max_amount);
    bs.write(item.extra_file);
    bs.write(item.extra_file_hash);
    bs.write(item.animation_time);
    bs.write(item.pet_name);
    bs.write(item.pet_prefix);
    bs.write(item.pet_suffix);
    bs.write(item.pet_ability);
    bs.write(item.seed_base);
    bs.write(item.seed_overlay);
    bs.write(item.tree_base);
    bs.write(item.tree_leaves);
    bs.write(item.seed_color);
    bs.write(item.seed_overlay_color);
    bs.write(item.ingredient);
    bs.write(item.grow_time);
    bs.write(item.fx_flag);
    bs.write(item.animating_coordinates);
    bs.write(item.animating_texture_file);
    bs.write(item.animating_coordinates_2);
    bs.write(item.__unk_1);
    bs.write(item.__unk_2);
    bs.write(item.flags_2);
    bs.write(item.__unk_3);
    bs.write(item.tile_range);
    bs.write(item.vault_capacity);
    bs.write(item.punch_options);
    bs.write(item.__unk_4);
    bs.write(item.body_part_list);
    bs.write(item.light_range);
    bs.write(item.__unk_5);
    bs.write(item.can_sit);
    bs.write(item.player_offset_x);
    bs.write(item.player_offset_y);
    bs.write(item.chair_texture_x);
    bs.write(item.chair_texture_y);
    bs.write(item.chair_leg_offset_x);
    bs.write(item.chair_leg_offset_y);
    bs.write(item.chair_texture_file);
    bs.write(item.renderer_data_file);
    bs.write(item.__unk_6);
    bs.write(item.renderer_data_file_hash);
    bs.write(item.__unk_7);
    bs.write(item.__unk_8);
    bs.write(item.info);
    bs.write(item.ingredients);

    if (version >= 24) {
        bs.write(item.__unk_9);
    }
    if (version >= 25) {
        bs.write(std::string{ id == 1 ? "BandageCannonPlayerHitFx" : "" });
        bs.write<std::uint32_t>(id == 1 ? 3000 : 0);
    }
    if (version >= 26) {
        bs.write<std::uint8_t>(id == 1 ? 1 : 0);
    }
    if (version >= 27) {
        bs.write(std::string{ id == 1 ? "6" : "" });
    }
}

std::vector<std::byte> make_items_dat(const std::uint16_t version, const std::uint32_t count, const std::uint16_t record_version)
{
    utils::ByteStream<> bs{};
    bs.write(version);
    bs.write(count);
    for (std::uint32_t id{ 0 }; id < count; ++id) {
        write_item(bs, id, record_version);
    }
    return bs.get_data();
}
}

class ItemDatabaseTest : public ::testing::Test {
protected:
    void SetUp() override { ItemDatabase::instance().clear(); }
    void TearDown() override { ItemDatabase::instance().clear(); }
};

TEST_F(ItemDatabaseTest, ParsesEverySupportedVersion)
{
    for (std::uint16_t version{ 24 }; version <= ItemDatabase::MAX_SUPPORTED_VERSION; ++version) {
        const auto data{ make_items_dat(version, 3, version) };
        ASSERT_TRUE(ItemDatabase::instance().parse(data)) << "version " << version;
        EXPECT_EQ(ItemDatabase::instance().get_version(), version);
        EXPECT_EQ(ItemDatabase::instance().get_count(), 3u);
        ASSERT_NE(ItemDatabase::instance().get_item(2), nullptr);
        EXPECT_EQ(ItemDatabase::instance().get_item(2)->item_id, 2u);
    }

    const auto* item{ ItemDatabase::instance().get_item(1) };
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->hit_sound_fx, "BandageCannonPlayerHitFx");
    EXPECT_EQ(item->hit_sound_fx_hash, 3000u);
    EXPECT_EQ(item->__unk_10, 1);
    EXPECT_EQ(item->__unk_11, "6");
}

// Regression: a v25+ file read with the v24 layout used to "succeed" with
// garbage ids; an unknown layout must be rejected and keep the old database.
TEST_F(ItemDatabaseTest, RejectsLayoutItDoesNotUnderstandAndKeepsOldDatabase)
{
    ASSERT_TRUE(ItemDatabase::instance().parse(make_items_dat(27, 2, 27)));

    // Header says v24 but the records carry v27 fields.
    EXPECT_FALSE(ItemDatabase::instance().parse(make_items_dat(24, 3, 27)));
    EXPECT_EQ(ItemDatabase::instance().get_version(), 27);
    EXPECT_EQ(ItemDatabase::instance().get_count(), 2u);
}

TEST_F(ItemDatabaseTest, RejectsTruncatedDataAndHugeCounts)
{
    auto truncated{ make_items_dat(27, 3, 27) };
    truncated.resize(truncated.size() - 10);
    EXPECT_FALSE(ItemDatabase::instance().parse(truncated));

    auto huge{ make_items_dat(27, 1, 27) };
    huge[2] = huge[3] = huge[4] = huge[5] = std::byte{ 0xFF }; // count = 0xFFFFFFFF
    EXPECT_FALSE(ItemDatabase::instance().parse(huge));
    EXPECT_TRUE(ItemDatabase::instance().empty());
}

TEST_F(ItemDatabaseTest, HashOfLoadedDataMatchesProton)
{
    const auto data{ make_items_dat(27, 2, 27) };
    ASSERT_TRUE(ItemDatabase::instance().parse(data));
    EXPECT_EQ(
        ItemDatabase::instance().get_hash(),
        utils::hash::proton(reinterpret_cast<const char*>(data.data()), data.size())
    );
}

TEST_F(ItemDatabaseTest, BundledItemsDatParsesExactly)
{
    const std::filesystem::path path{ GTPROXY_TEST_RESOURCES_DIR "/items.dat" };
    ASSERT_TRUE(std::filesystem::exists(path));

    ASSERT_TRUE(ItemDatabase::instance().load_from_file(path.string()));
    EXPECT_EQ(ItemDatabase::instance().get_version(), 23);
    EXPECT_EQ(ItemDatabase::instance().get_count(), 21700u);
    ASSERT_NE(ItemDatabase::instance().get_item(242), nullptr);
    EXPECT_EQ(ItemDatabase::instance().get_item(242)->item_name, "World Lock");
    // Independently computed unsigned Proton hash of this file.
    EXPECT_EQ(ItemDatabase::instance().get_hash(), 2440345856u);
}
