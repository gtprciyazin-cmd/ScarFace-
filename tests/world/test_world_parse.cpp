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

    // The access list is clamped to what is left (nothing), and the truncated
    // tile stops parsing instead of reading on out of sync.
    EXPECT_TRUE(world.get_tile_map().get_tiles().empty());
    EXPECT_TRUE(world.get_object_map().get_objects().empty());
}

namespace {
void write_bytes(utils::ByteStream<>& bs, const std::initializer_list<std::uint8_t> bytes)
{
    for (const auto b : bytes) {
        bs.write<std::uint8_t>(b);
    }
}
}

// Tiles copied byte for byte from a real SendMapData (world version 0x1b)
// captured through the proxy; the parser must stay in sync across all of them
// and reach the object list.
TEST(WorldParseTest, StaysInSyncOnRealWorldTiles)
{
    item::ItemDatabase::instance().clear();

    auto bs{ map_header(10, 1, 10) };
    // Small Lock (extra type 3) owned by 0x05c109d9, no access list
    write_bytes(bs, { 0xce, 0x00, 0x90, 0x19, 0x00, 0x00, 0x01, 0x00, 0x03, 0x01, 0xd9, 0x09, 0xc1, 0x05,
                      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });
    // Sign (extra type 2) "HI" + end marker
    write_bytes(bs, { 0x60, 0x02, 0x86, 0x04, 0x00, 0x00, 0x01, 0x00, 0x02, 0x02, 0x00, 'H', 'I',
                      0xff, 0xff, 0xff, 0xff });
    // Extra type 58 (no payload) with colour flags
    write_bytes(bs, { 0x72, 0x12, 0x2e, 0x02, 0x00, 0x00, 0x01, 0xe0, 0x3a });
    // Main door (extra type 1) "EXIT"
    write_bytes(bs, { 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x04, 0x00, 'E', 'X', 'I', 'T', 0x00 });
    // Seed (extra type 4) under a lock
    write_bytes(bs, { 0xe7, 0x03, 0x90, 0x19, 0x5d, 0x0a, 0x13, 0x00, 0x5d, 0x0a, 0x04, 0x84, 0x71, 0x00, 0x00, 0x01 });
    // Display block (extra type 23) showing item 0x2f6
    write_bytes(bs, { 0x82, 0x0b, 0x86, 0x04, 0x00, 0x00, 0x01, 0x00, 0x17, 0xf6, 0x02, 0x00, 0x00 });
    // Data bedrock (extra type 42, 21 bytes)
    write_bytes(bs, { 0xb0, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x2a, 0x00, 0x01, 0x00, 0x00, 0x00,
                      0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff });
    // Two locked plain tiles
    write_bytes(bs, { 0x12, 0x05, 0x00, 0x00, 0xc9, 0x08, 0x02, 0x20, 0xc9, 0x08 });
    write_bytes(bs, { 0xfa, 0x0a, 0x90, 0x19, 0xc9, 0x08, 0x22, 0x20, 0xc9, 0x08 });
    // Plain empty tile
    write_plain_tile(bs, 0, 0);

    write_object_map(bs, 1);
    bs.write<std::uint16_t>(242); // dropped World Lock
    bs.write<float>(32.0f);
    bs.write<float>(64.0f);
    bs.write<std::uint8_t>(5);
    bs.write<std::uint8_t>(0);
    bs.write<std::uint32_t>(3);

    World world{};
    parse(world, bs);

    const auto& tiles{ world.get_tile_map().get_tiles() };
    ASSERT_EQ(tiles.size(), 10u);

    const auto* lock{ tiles[0].extra.get_as<tile_extra::Lock>() };
    ASSERT_NE(lock, nullptr);
    EXPECT_EQ(lock->owner_id, 0x05c109d9u);

    const auto* sign{ tiles[1].extra.get_as<tile_extra::Sign>() };
    ASSERT_NE(sign, nullptr);
    EXPECT_EQ(sign->label, "HI");

    const auto* door{ tiles[3].extra.get_as<tile_extra::Door>() };
    ASSERT_NE(door, nullptr);
    EXPECT_EQ(door->label, "EXIT");

    const auto* seed{ tiles[4].extra.get_as<tile_extra::Seed>() };
    ASSERT_NE(seed, nullptr);
    EXPECT_EQ(seed->fruit_count, 1);
    EXPECT_EQ(tiles[4].lock_parent_tile, 0x0a5d);

    const auto* display{ tiles[5].extra.get_as<tile_extra::DisplayBlock>() };
    ASSERT_NE(display, nullptr);
    EXPECT_EQ(display->item_id, 0x2f6u);

    EXPECT_EQ(tiles[7].foreground, 0x0512);
    EXPECT_EQ(tiles[8].foreground, 0x0afa);

    EXPECT_TRUE(world.get_tile_map().is_complete());
    EXPECT_TRUE(world.objects_known());

    const auto& objects{ world.get_object_map().get_objects() };
    ASSERT_EQ(objects.size(), 1u);
    EXPECT_EQ(objects[0].item_id, 242);
    EXPECT_EQ(objects[0].amount, 5);
    EXPECT_EQ(objects[0].object_id, 3u);
}

TEST(WorldParseTest, UnknownExtraTypeStopsInsteadOfDesyncing)
{
    item::ItemDatabase::instance().clear();

    auto bs{ map_header(2, 1, 2) };
    write_plain_tile(bs, 2, 0);
    bs.write<std::uint16_t>(9999);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(0x0001);
    bs.write<std::uint8_t>(5); // extra type with no known layout
    write_object_map(bs, 0);

    World world{};
    parse(world, bs);

    ASSERT_EQ(world.get_tile_map().get_tiles().size(), 1u);
    EXPECT_EQ(world.get_tile_map().get_tiles()[0].foreground, 2);
    EXPECT_TRUE(world.get_object_map().get_objects().empty());

    // Nothing after the unknown tile can be trusted, drop ids included.
    EXPECT_FALSE(world.get_tile_map().is_complete());
    EXPECT_FALSE(world.objects_known());
}

TEST(WorldParseTest, ReadsCborTrailerAndNewExtraTypes)
{
    item::ItemDatabase::instance().clear();

    auto bs{ map_header(4, 1, 4) };
    // Battle Pet Cage: no extra flag, but a CBOR trailer of 3 bytes
    bs.write<std::uint16_t>(3548);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint32_t>(3);
    write_bytes(bs, { 0xa1, 0x01, 0x02 });
    // Angelic counting cloud (75) in state 2: u32 + u16 + u8
    bs.write<std::uint16_t>(100);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(0x0001);
    bs.write<std::uint8_t>(75);
    bs.write<std::uint32_t>(2);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint8_t>(0);
    // Friends entrance (81) with two friends: u32, 2 bytes, u16 count, 2 x u32
    bs.write<std::uint16_t>(101);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(0x0001);
    bs.write<std::uint8_t>(81);
    bs.write<std::uint32_t>(7);
    bs.write<std::uint16_t>(0);
    bs.write<std::uint16_t>(2);
    bs.write<std::uint32_t>(11);
    bs.write<std::uint32_t>(12);
    write_plain_tile(bs, 8, 0);
    write_object_map(bs, 0);

    World world{};
    parse(world, bs);

    const auto& tiles{ world.get_tile_map().get_tiles() };
    ASSERT_EQ(tiles.size(), 4u);
    EXPECT_EQ(tiles[3].foreground, 8);
    EXPECT_EQ(world.get_object_map().get_drop_id(), 7u);
}
