#include <gtest/gtest.h>
#include "packet/game/server.hpp"

using namespace packet;

namespace {
PacketVariant round_trip_send_to_server(const PacketVariant& from_server)
{
    game::OnSendToServer pkt{};
    EXPECT_TRUE(pkt.read(VariantPayload{ from_server }));

    // What ConnectionHandler does before forwarding it to the client.
    pkt.address = "127.0.0.1";
    pkt.port = 16999;

    const auto payload{ pkt.write() };
    const auto* var{ std::get_if<VariantPayload>(&payload) };
    EXPECT_NE(var, nullptr);
    return var ? var->variant : PacketVariant{};
}
}

// Regression: the redirect was rebuilt from scratch, which turned the SIGNED
// login mode into an UNSIGNED 0 and appended an empty username. These are the
// two OnSendToServer packets from a real login (user log, 2026-10-20).
TEST(ServerPacketTest, SendToServerKeepsServerArguments)
{
    const PacketVariant first{
        std::string{ "OnSendToServer" },
        int32_t{ 17100 },
        int32_t{ 5024593 },
        int32_t{ 108338646 },
        std::string{ "213.179.209.177|0|027C462725024DBE097444364E3C6D13" },
        int32_t{ 1 },
        std::string{ "RemeKingOfLegend" }
    };

    const auto out{ round_trip_send_to_server(first) };
    ASSERT_EQ(out.size(), 7u);
    EXPECT_EQ(out.get<int32_t>(1), 16999);
    EXPECT_EQ(out.get<int32_t>(2), 5024593);
    EXPECT_EQ(out.get<int32_t>(3), 108338646);
    EXPECT_EQ(out.get<std::string>(4), "127.0.0.1|0|027C462725024DBE097444364E3C6D13");
    ASSERT_TRUE(std::holds_alternative<int32_t>(out.get_variants()[5]));
    EXPECT_EQ(out.get<int32_t>(5), 1);
    EXPECT_EQ(out.get<std::string>(6), "RemeKingOfLegend");
}

TEST(ServerPacketTest, SendToServerWithoutUsernameStaysWithoutIt)
{
    const PacketVariant sub_server{
        std::string{ "OnSendToServer" },
        int32_t{ 17028 },
        int32_t{ -1 },
        int32_t{ 108338646 },
        std::string{ "213.179.209.175|0|-1" },
        int32_t{ 0 }
    };

    const auto out{ round_trip_send_to_server(sub_server) };
    ASSERT_EQ(out.size(), 6u);
    EXPECT_EQ(out.get<int32_t>(1), 16999);
    EXPECT_EQ(out.get<int32_t>(2), -1);
    EXPECT_EQ(out.get<std::string>(4), "127.0.0.1|0|-1");
    ASSERT_TRUE(std::holds_alternative<int32_t>(out.get_variants()[5]));
    EXPECT_EQ(out.get<int32_t>(5), 0);
}

TEST(ServerPacketTest, SuperMainReadsHashAndKeepsSettingsString)
{
    const PacketVariant from_server{
        std::string{ "OnSuperMainStartAcceptLogonHrdxs47254722215a" },
        uint32_t{ 966734738 },
        std::string{ "ubistatic-a.akamaihd.net" },
        std::string{ "0098/1234/cache/" },
        std::string{ "cc.cz.madkite.freedom" },
        std::string{ "proto=227|choosemusic=audio/mp3/about_theme.mp3" }
    };

    game::OnSuperMainStartAcceptLogonHrdxs47254722215a pkt{};
    ASSERT_TRUE(pkt.read(VariantPayload{ from_server }));
    EXPECT_EQ(pkt.item_hash, 966734738u);
    EXPECT_FALSE(pkt.item_hash_signed);
    EXPECT_EQ(pkt.u, "ubistatic-a.akamaihd.net");

    const auto payload{ pkt.write() };
    const auto* var{ std::get_if<VariantPayload>(&payload) };
    ASSERT_NE(var, nullptr);
    ASSERT_EQ(var->variant.size(), 6u);
    EXPECT_EQ(var->variant.get<uint32_t>(1), 966734738u);
    EXPECT_EQ(var->variant.get<std::string>(5), "proto=227|choosemusic=audio/mp3/about_theme.mp3");
}

#include <cstring>
#include "packet/game/dialog.hpp"
#include "packet/game/message.hpp"

namespace {
GameUpdatePacket header_of(const std::vector<std::byte>& data)
{
    GameUpdatePacket header{};
    std::memcpy(&header, data.data() + sizeof(NetMessageType), sizeof(header));
    return header;
}
}

// Regression: proxy-built function calls went out with net_id 0, which the
// client runs on player 0 (nobody), so the /proxy menu never appeared.
TEST(ServerPacketTest, ProxyBuiltFunctionCallsTargetTheClient)
{
    game::OnDialogRequest dialog{};
    dialog.dialog = "add_label|Scarface|\nend_dialog|scarface_options|Close|Save|\n";
    const auto dialog_header{ header_of(PacketHelper::serialize(dialog)) };
    EXPECT_EQ(dialog_header.type, PACKET_CALL_FUNCTION);
    EXPECT_EQ(dialog_header.net_id, 0xFFFFFFFFu);

    // The roulette bubble names its target in the arguments, not the header.
    game::OnTalkBubble bubble{};
    bubble.net_id = 12;
    bubble.text = "`9[Spin]`` `412``";
    const auto data{ PacketHelper::serialize(bubble) };
    EXPECT_EQ(header_of(data).net_id, 0xFFFFFFFFu);

    PacketVariant args{};
    ASSERT_TRUE(args.deserialize(std::span{ data }.subspan(sizeof(NetMessageType) + sizeof(GameUpdatePacket))));
    EXPECT_EQ(args.get<std::string>(0), "OnTalkBubble");
    EXPECT_EQ(args.get_integer(1), 12);
    EXPECT_EQ(args.get<std::string>(2), "`9[Spin]`` `412``");
}

TEST(ServerPacketTest, SendToServerBuiltFromScratchCarriesEveryField)
{
    game::OnSendToServer pkt{};
    pkt.port = 17000;
    pkt.token = 5;
    pkt.user = 6;
    pkt.address = "1.2.3.4";
    pkt.door_id = "0";
    pkt.uuid_token = "abc";
    pkt.login_mode = 1;
    pkt.username = "Name";

    const auto payload{ pkt.write() };
    const auto* var{ std::get_if<VariantPayload>(&payload) };
    ASSERT_NE(var, nullptr);
    ASSERT_EQ(var->variant.size(), 7u);
    EXPECT_EQ(var->variant.get<std::string>(4), "1.2.3.4|0|abc");
    EXPECT_EQ(var->variant.get_integer(5), 1);
    EXPECT_EQ(var->variant.get<std::string>(6), "Name");
}

// An argument type the proxy does not know must not hide the call: the
// arguments before it are kept (an unseen OnSendToServer would let the client
// connect around the proxy).
TEST(ServerPacketTest, UnknownTrailingArgumentTypeKeepsLeadingArguments)
{
    const PacketVariant known{
        std::string{ "OnSendToServer" },
        int32_t{ 17100 },
        int32_t{ 1 },
        int32_t{ 2 },
        std::string{ "213.179.209.177|0|x" },
        int32_t{ 1 }
    };
    auto bytes{ known.serialize() };
    bytes[0] = std::byte{ 7 };                 // one more argument...
    bytes.push_back(std::byte{ 6 });           // ...at index 6
    bytes.push_back(std::byte{ 8 });           // of an unknown type
    for (int i{ 0 }; i < 16; ++i) {
        bytes.push_back(std::byte{ 0 });
    }

    PacketVariant parsed{};
    ASSERT_TRUE(parsed.deserialize(bytes));
    ASSERT_EQ(parsed.size(), 6u);
    EXPECT_EQ(parsed.get<std::string>(0), "OnSendToServer");
    EXPECT_EQ(parsed.get_integer(1), 17100);
}
