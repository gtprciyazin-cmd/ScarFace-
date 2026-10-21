#include <gtest/gtest.h>
#include "utils/discord_ipc.hpp"

using namespace utils::discord;

TEST(DiscordIpcTest, FrameHasLittleEndianOpcodeAndLength)
{
    const auto frame{ make_frame(Opcode::Frame, "{}") };
    ASSERT_EQ(frame.size(), 10u);
    EXPECT_EQ(frame[0], std::byte{ 1 });
    EXPECT_EQ(frame[4], std::byte{ 2 });
    EXPECT_EQ(frame[8], std::byte{ '{' });

    const auto header{ read_header(frame.data(), frame.size()) };
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->opcode, Opcode::Frame);
    EXPECT_EQ(header->length, 2u);
}

TEST(DiscordIpcTest, RejectsGarbageHeaders)
{
    const std::vector<std::byte> bad(8, std::byte{ 0xff });
    EXPECT_FALSE(read_header(bad.data(), bad.size()).has_value());
    EXPECT_FALSE(read_header(bad.data(), 4).has_value());
}

TEST(DiscordIpcTest, HandshakeNamesTheApplication)
{
    EXPECT_EQ(handshake_json("123"), R"({"v":1,"client_id":"123"})");
}

TEST(DiscordIpcTest, ActivityLeavesOutEmptyFieldsAndButtons)
{
    Activity activity{};
    activity.details = "Currently In-game";
    activity.state = "Licensed Role: Premium Proxy User";
    activity.start_timestamp = 1700000000;
    activity.large_image = "scarface";
    activity.buttons = { { "Scarface Premium Proxy Discord", "https://discord.gg/x" }, { "Website", "" } };

    EXPECT_EQ(
        set_activity_json(42, activity, 7),
        R"({"cmd":"SET_ACTIVITY","args":{"pid":42,"activity":{"details":"Currently In-game","state":"Licensed Role: Premium Proxy User","timestamps":{"start":1700000000},"assets":{"large_image":"scarface"},"buttons":[{"label":"Scarface Premium Proxy Discord","url":"https://discord.gg/x"}]}},"nonce":"7"})"
    );
}

TEST(DiscordIpcTest, EscapesJsonStrings)
{
    EXPECT_EQ(json_escape("a\"b\\c\n"), "a\\\"b\\\\c\\n");
}

TEST(DiscordIpcTest, FindsEventsWithAnySpacing)
{
    EXPECT_TRUE(has_string_field(R"({"cmd":"DISPATCH","evt":"READY"})", "evt", "READY"));
    EXPECT_TRUE(has_string_field(R"({"cmd": "DISPATCH", "evt" : "READY"})", "evt", "READY"));
    EXPECT_FALSE(has_string_field(R"({"evt":null,"data":{"note":"READY"}})", "evt", "READY"));
}
