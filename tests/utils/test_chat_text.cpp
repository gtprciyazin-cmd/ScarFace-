#include <gtest/gtest.h>
#include "utils/chat_text.hpp"

using namespace utils;

// A newline or pipe in the spam text would end the chat line and let the rest
// be read as another field of the packet sent to the server.
TEST(ChatTextTest, SanitizeDropsCharactersThatBreakThePacket)
{
    EXPECT_EQ(sanitize_chat_message("hi\nthere|you\r\t!", 120), "hithereyou!");
    EXPECT_EQ(sanitize_chat_message("   `2buy`` wl   ", 120), "`2buy`` wl");
    EXPECT_EQ(sanitize_chat_message(" \n | ", 120), "");
}

TEST(ChatTextTest, SanitizeCutsToTheLimitWithoutALoneBacktick)
{
    EXPECT_EQ(sanitize_chat_message("abcdef", 4), "abcd");
    EXPECT_EQ(sanitize_chat_message("abc`2def", 4), "abc");
}

TEST(ChatTextTest, PingIsColouredBySpeed)
{
    EXPECT_EQ(format_ping(45), "`245 ms``");
    EXPECT_EQ(format_ping(150), "`9150 ms``");
    EXPECT_EQ(format_ping(320), "`4320 ms``");
}

// The ping goes after the name's own colours are closed, so a name like
// "`^Player" does not tint it.
TEST(ChatTextTest, NameWithPingClosesTheNameColourFirst)
{
    EXPECT_EQ(name_with_ping("`^Player``", 45), "`^Player```` `2(45 ms)``");
    EXPECT_EQ(name_with_ping("`wDr.Player", 230), "`wDr.Player`` `4(230 ms)``");
}

// The legend flag depends on the name ending in " of Legend``".
TEST(ChatTextTest, NameWithPingKeepsTheLegendSuffixLast)
{
    EXPECT_EQ(name_with_ping("`9Player of Legend``", 45), "`9Player`` `2(45 ms)```9 of Legend``");
    EXPECT_TRUE(name_with_ping("`9Player of Legend``", 45).ends_with(" of Legend``"));
}
