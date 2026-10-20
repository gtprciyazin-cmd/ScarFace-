#include <cstring>
#include <gtest/gtest.h>
#include "packet/packet_decoder.hpp"
#include "packet/register_packets.hpp"
#include "packet/game/inventory.hpp"
#include "player/inventory.hpp"

using namespace packet;

namespace {
// Builds [outer type][56-byte header][inventory extra][NUL] the way the
// server sends a tank packet: 57 items give exactly the 296 bytes seen in the
// user's log under an outer type the proxy did not know.
std::vector<std::byte> inventory_packet(const std::uint32_t outer_type, const std::uint16_t items)
{
    utils::ByteStream<> extra{};
    extra.write<std::uint8_t>(1);
    extra.write<std::uint32_t>(64);
    extra.write<std::uint16_t>(items);
    for (std::uint16_t i{ 0 }; i < items; ++i) {
        extra.write<std::uint16_t>(static_cast<std::uint16_t>(i == 0 ? 242 : 2 + i));
        extra.write<std::uint8_t>(static_cast<std::uint8_t>(i == 0 ? 37 : 1));
        extra.write<std::uint8_t>(0);
    }
    const auto extra_bytes{ extra.get_data() };

    GameUpdatePacket header{};
    header.type = PACKET_SEND_INVENTORY_STATE;
    header.net_id = static_cast<std::uint32_t>(-1);
    header.flags.extended = 1;
    header.data_size = static_cast<std::uint32_t>(extra_bytes.size());

    utils::ByteStream<> bs{};
    bs.write(outer_type);
    bs.write(header);
    bs.write_data(extra_bytes.data(), extra_bytes.size());
    bs.write<std::uint8_t>(0);
    return bs.get_data();
}

const core::Config::LogConfig& quiet()
{
    static const core::Config::LogConfig config{ false, false, false, false };
    return config;
}
}

TEST(PacketDecoderTest, GamePacketBehindUnknownOuterTypeIsRead)
{
    register_all_packets();

    const auto data{ inventory_packet(10, 57) };
    ASSERT_EQ(data.size(), 296u);

    const auto decoded{ PacketDecoder::decode(data, quiet(), "ClientBound") };
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ((*decoded)->id(), PacketId::SendInventoryState);

    const auto* inventory_pkt{ dynamic_cast<const game::SendInventoryState*>(decoded->get()) };
    ASSERT_NE(inventory_pkt, nullptr);

    player::Inventory inventory{};
    ASSERT_TRUE(inventory.parse(inventory_pkt->extra));
    EXPECT_EQ(inventory.get_items().size(), 57u);
    EXPECT_EQ(inventory.get(242), 37);
}

TEST(PacketDecoderTest, OrdinaryInventoryPacketStillDecodes)
{
    register_all_packets();

    const auto decoded{ PacketDecoder::decode(inventory_packet(NET_MESSAGE_GAME_PACKET, 3), quiet(), "ClientBound") };
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ((*decoded)->id(), PacketId::SendInventoryState);
}

TEST(PacketDecoderTest, TrackTextIsNotMistakenForAGamePacket)
{
    register_all_packets();

    std::string text{ "eventName|102_PLAYER.AUTHENTICATION\nAuthenticated|1\nLevel|12\nGrowId|SomePlayer\n" };
    text.resize(165, 'x');
    utils::ByteStream<> bs{};
    bs.write<std::uint32_t>(NET_MESSAGE_TRACK);
    bs.write_data(text.data(), text.size());
    const auto data{ bs.get_data() };
    ASSERT_EQ(data.size(), 169u);

    EXPECT_FALSE(PacketDecoder::decode(data, quiet(), "ClientBound").has_value());
}
