#include <gtest/gtest.h>
#include "utils/roulette.hpp"

using namespace utils::roulette;

TEST(RouletteTest, StripsColorCodes)
{
    EXPECT_EQ(strip_color_codes("`7[```wPlayer`` got `422``!`7]``"), "[Player got 22!]");
    EXPECT_EQ(strip_color_codes("plain"), "plain");
    EXPECT_EQ(strip_color_codes("trailing`"), "trailing");
}

TEST(RouletteTest, ParsesServerSpinMessage)
{
    const auto spin{ parse_spin_message("`7[```wPlayer`` spun the wheel and got `422``!`7]``") };

    ASSERT_TRUE(spin.has_value());
    EXPECT_EQ(spin->spinner, "Player");
    EXPECT_EQ(spin->number, 22);
    EXPECT_EQ(spin->color_code, "`4");
}

TEST(RouletteTest, ParsesMessageWithoutColorCodes)
{
    const auto spin{ parse_spin_message("[Some Player spun the wheel and got 0!]") };

    ASSERT_TRUE(spin.has_value());
    EXPECT_EQ(spin->spinner, "Some Player");
    EXPECT_EQ(spin->number, 0);
    EXPECT_EQ(spin->color_code, "`2");
}

TEST(RouletteTest, FallsBackToWheelColors)
{
    EXPECT_EQ(parse_spin_message("A spun the wheel and got 1!")->color_code, "`4");
    EXPECT_EQ(parse_spin_message("A spun the wheel and got 2!")->color_code, "`b");
    EXPECT_EQ(parse_spin_message("A spun the wheel and got 36!")->color_code, "`4");
}

TEST(RouletteTest, Reme)
{
    EXPECT_EQ(reme(0), 0);
    EXPECT_EQ(reme(7), 7);
    EXPECT_EQ(reme(22), 4);
    EXPECT_EQ(reme(19), 0);
    EXPECT_EQ(reme(28), 0);
    EXPECT_EQ(reme(36), 9);
}

TEST(RouletteTest, RejectsOtherMessages)
{
    EXPECT_FALSE(parse_spin_message("Hello world").has_value());
    EXPECT_FALSE(parse_spin_message("A spun the wheel and got !").has_value());
    EXPECT_FALSE(parse_spin_message("A spun the wheel and got 37!").has_value());
    EXPECT_FALSE(parse_spin_message("A spun the wheel and got 123!").has_value());
}

// Shapes captured from live servers (Feb 2026) and the chat prefixes the
// server puts in front of anything a player types.
TEST(RouletteTest, RealSpinNeedsTheServerDelayAndNoChatPrefix)
{
    const std::string real{ "`7[```wGrowtopiaProxy`` spun the wheel and got `47``!`7]``" };
    EXPECT_TRUE(utils::roulette::is_real_spin(real, 2000));
    EXPECT_TRUE(utils::roulette::is_real_spin(real, 2000, std::string_view{ "`wGrowtopiaProxy``" }));

    // Shown at once: typed, not spun.
    EXPECT_FALSE(utils::roulette::is_real_spin(real, 0));
    // A bubble over someone else's head.
    EXPECT_FALSE(utils::roulette::is_real_spin(real, 2000, std::string_view{ "`wSomeoneElse``" }));
    // Typed into chat, even with a made-up delay.
    EXPECT_FALSE(utils::roulette::is_real_spin("CP:0_PL:0_OID:_player_chat=" + real, 2000));
    EXPECT_FALSE(utils::roulette::is_real_spin("CP:0_PL:0_OID:_CT:[W]_ `6<`wBob``>`` `$" + real, 2000));
}

TEST(RouletteTest, TypedSpinsAreStillParsedBehindTheChatPrefix)
{
    const auto spin{ utils::roulette::parse_spin_message("CP:0_PL:0_OID:_player_chat=`7[```wBob`` spun the wheel and got `20``!`7]``") };
    ASSERT_TRUE(spin.has_value());
    EXPECT_EQ(spin->spinner, "Bob");
    EXPECT_EQ(spin->number, 0);
}

TEST(RouletteTest, TagsGoBehindTheChatPrefix)
{
    EXPECT_EQ(utils::roulette::tag_message("`7[```wA`` spun the wheel and got `47``!`7]``", "`2[REAL]``"),
        "`2[REAL]`` `7[```wA`` spun the wheel and got `47``!`7]``");
    EXPECT_EQ(utils::roulette::tag_message("CP:0_PL:0_OID:_player_chat=hi", "`4[FAKE]``"),
        "CP:0_PL:0_OID:_player_chat=`4[FAKE]`` hi");
    EXPECT_EQ(utils::roulette::tag_message("CP:0_PL:0_OID:_CT:[W]_ `6<`wBob``>`` hi", "`4[FAKE]``"),
        "CP:0_PL:0_OID:_CT:[W]_ `4[FAKE]`` `6<`wBob``>`` hi");
}

TEST(RouletteTest, TitlesDoNotMakeARealSpinFake)
{
    const std::string spin{ "`7[```9Bob of Legend`` spun the wheel and got `47``!`7]``" };
    EXPECT_TRUE(utils::roulette::is_real_spin(spin, 2000, std::string_view{ "`9Bob of Legend``" }));
    EXPECT_TRUE(utils::roulette::is_real_spin("`7[```wDr. Bob of Legend`` spun the wheel and got `47``!`7]``", 2000, std::string_view{ "`wDr. Bob of Legend``" }));
    EXPECT_TRUE(utils::roulette::is_real_spin(spin, 2000, std::string_view{ "`wBob``" }));
}
