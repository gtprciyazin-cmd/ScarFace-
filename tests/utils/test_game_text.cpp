#include <gtest/gtest.h>
#include "utils/game_text.hpp"

using namespace utils::game_text;

TEST(GameTextTest, ReadsAnAccessOffer)
{
    const auto offer{ parse_access_offer("`wSomeOwner`w wants to add you to a World Lock`w. Wrench yourself to accept.") };
    ASSERT_TRUE(offer.has_value());
    EXPECT_EQ(offer->owner, "SomeOwner");
    EXPECT_EQ(offer->lock, "World Lock");

    const auto huge{ parse_access_offer("Bob wants to add you to a Huge Lock. Wrench yourself to accept.") };
    ASSERT_TRUE(huge.has_value());
    EXPECT_EQ(huge->lock, "Huge Lock");
}

// Typing the sentence in world chat must not make the proxy wrench anyone.
TEST(GameTextTest, PlayerChatIsNeverAnOffer)
{
    EXPECT_FALSE(parse_access_offer("CP:0_PL:4_OID:_CT:[W]_ `6<`wTroll``>`` x wants to add you to a World Lock. Wrench yourself to accept.").has_value());
    EXPECT_FALSE(parse_access_offer("Nothing to see here").has_value());
}

TEST(GameTextTest, ReadsTheAmountFromTheTrashDialog)
{
    EXPECT_EQ(trash_owned_amount("add_textbox|How many to `4destroy``? (you have 37)|left|\nadd_text_input|count||0|5|"), 37);
    EXPECT_EQ(trash_owned_amount("add_text_input|count||0|5|"), 0);
}
