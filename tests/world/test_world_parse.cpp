#include <gtest/gtest.h>
#include "item/item_database.hpp"
#include "world/world.hpp"

using namespace world;

namespace {
// Builds SendMapData extra bytes in the layout World::serialize reads:
// version, 4 unknown bytes, name, then the tile map and the object map.
utils::ByteStream<> map_header(const std::uint32_t width, const std::uint32_t height, const std::uint32_t tile_count)
{
    utils::ByteStream<> bs{};
    bs.write<std::uint16_t>(0x1b);
    bs.write<std::uint32_t>(0x40);
    bs.write(std::string{ "TESTWORLD" });
    bs.write<std::uint32_t>(width);
    bs.write<std::uint32_t>(height);
    bs.write<std::uint32_t>(tile_count);
    for (int i{ 0 }; i < 5; ++i) {
        bs.write<std::uint8_t>(0);
    }
    return bs;
}

void write_plain_tile(utils::ByteStream<>& bs, const std::uint16_t fg, const std::uint16_t bg)
{
    bs.write<std::uint16_t>(fg);
    bs.write<std::uint16_t>(bg);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(0);
}

void write_object_map(utils::ByteStream<>& bs, const std::uint32_t count)
{
    for (int i{ 0 }; i < 12; ++i) {
        bs.write<std::uint8_t>(0);
    }
    bs.write<std::uint32_t>(count);
    bs.write<std::uint32_t>(7); // drop id
}

void parse(World& world, const utils::ByteStream<>& bs)
{
    const auto data{ bs.get_data() };
    world.serialize(data.data(), data.size());
}
}

// Regression: entering a world crashed the proxy when items.dat had not been
// loaded, because every plain tile looked its foreground up in the empty DB.
TEST(WorldParseTest, ParsesTilesWithEmptyItemDatabase)
{
    item::ItemDatabase::instance().clear();

    auto bs{ map_header(2, 1, 2) };
    write_plain_tile(bs, 2, 14);
    write_plain_tile(bs, 8, 0);
    write_object_map(bs, 0);

    World world{};
    parse(world, bs);

    const auto& tiles{ world.get_tile_map().get_tiles() };
    ASSERT_EQ(tiles.size(), 2u);
    EXPECT_EQ(tiles[0].foreground, 2);
    EXPECT_EQ(tiles[0].background, 14);
    EXPECT_EQ(tiles[1].foreground, 8);
    EXPECT_EQ(world.get_object_map().get_drop_id(), 7u);
}

TEST(WorldParseTest, CorruptCountsDoNotAllocateOrReadPastTheEnd)
{
    item::ItemDatabase::instance().clear();

    auto bs{ map_header(100, 60, 0xFFFFFFFF) };
    write_plain_tile(bs, 2, 14);

    World world{};
    parse(world, bs);

    // Only as many tiles as the remaining bytes can hold, not 4 billion.
    EXPECT_LE(world.get_tile_map().get_tiles().size(), 1u);
    EXPECT_TRUE(world.get_object_map().get_objects().empty());
}

TEST(WorldParseTest, LockWithHugeAccessListIsClamped)
{
    item::ItemDatabase::instance().clear();

    auto bs{ map_header(1, 1, 1) };
    bs.write<std::uint16_t>(242); // World Lock
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(0x0001); // has extra data
    bs.write<std::uint8_t>(3); // lock
    bs.write<std::uint8_t>(1);
    bs.write<std::uint32_t>(12345); // owner
    bs.write<std::uint32_t>(0xFFFFFFF0); // corrupt access count

    World world{};
    parse(world, bs);

    const auto& tiles{ world.get_tile_map().get_tiles() };
    ASSERT_EQ(tiles.size(), 1u);
    const auto* lock{ tiles[0].extra.get_as<tile_extra::Lock>() };
    ASSERT_NE(lock, nullptr);
    EXPECT_EQ(lock->owner_id, 12345u);
    EXPECT_TRUE(lock->accesses.empty());
}
