#include <gtest/gtest.h>

#include "Application/AtReplySituation.h"

#include <string>
#include <vector>

namespace
{
InboundMessage atMessage()
{
    InboundMessage message;
    message.post_type = "message";
    message.message_type = "group";
    message.group_id = 1026024045;
    message.user_id = 1477646;
    message.message_id = 473430622;
    message.plain_text = "这张图里是谁？这是一句群聊原文";
    return message;
}

TEST(AtReplySituationTest, AssemblesDerivedCardForAtReply)
{
    const affect::SituationCard card = atReplySituation(
        atMessage(), "话题注记：崩三前瞻", {"inspect_image", "klein_web_search"}, 16);
    EXPECT_EQ(card.source, "at_reply");
    EXPECT_EQ(card.triggerKind, "at");
    EXPECT_EQ(card.groupId, 1026024045U);
    EXPECT_EQ(card.userId, 1477646U);
    EXPECT_EQ(card.recentSummary, "话题注记：崩三前瞻");
    EXPECT_EQ(card.availableTools, (std::vector<std::string>{"inspect_image", "klein_web_search"}));
    EXPECT_EQ(card.localHour, 16);
    EXPECT_EQ(card.turnCount, 0);
    EXPECT_EQ(card.secondsSinceLastTurn, -1);
    EXPECT_TRUE(card.planSummary.empty());
}

TEST(AtReplySituationTest, TriggerTextNeverEntersCard)
{
    const affect::SituationCard card = atReplySituation(atMessage(), "", {}, 0);
    EXPECT_EQ(card.toJson().dump().find("群聊原文"), std::string::npos);
    EXPECT_TRUE(card.recentSummary.empty());
}
} // namespace
