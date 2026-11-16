#include <gtest/gtest.h>

#include "Application/EngagementTurn.h"

#include <string>
#include <utility>
#include <vector>

namespace
{
ChatResponse withToolCalls(std::vector<ResponseToolCall> calls, const std::string &content = {})
{
    ChatResponse response;
    response.code = 200;
    response.finish_reason = "tool_calls";
    response.tool_calls = std::move(calls);
    response.content = content;
    return response;
}

ChatResponse textOnly(const std::string &content)
{
    ChatResponse response;
    response.code = 200;
    response.finish_reason = "stop";
    response.content = content;
    return response;
}

TEST(EngagementTurnTest, GroupSayIsSpeechWithItsText)
{
    const EngagementTurn say =
        parseEngagementTurn(withToolCalls({{"c1", "group_say", R"({"text":"大家好"})"}}));
    EXPECT_EQ(say.kind, EngagementTurn::Kind::Speak);
    EXPECT_EQ(say.text, "大家好");
    EXPECT_TRUE(say.reason.empty());
    EXPECT_EQ(say.unspokenChars, 0U);

    // 网关拼接的连续对象照常解析（parseToolArguments 合并，后者覆盖前者）
    const EngagementTurn joined =
        parseEngagementTurn(withToolCalls({{"c1", "group_say", R"({}{"text":"在的"})"}}));
    EXPECT_EQ(joined.kind, EngagementTurn::Kind::Speak);
    EXPECT_EQ(joined.text, "在的");
}

// text 只有空白：判定仍是发言、text 原样交给调用方；runTurn 再按沉默处理（行为未变）
TEST(EngagementTurnTest, GroupSayWithBlankTextIsLeftToCaller)
{
    const EngagementTurn blank =
        parseEngagementTurn(withToolCalls({{"c1", "group_say", R"({"text":"  "})"}}));
    EXPECT_EQ(blank.kind, EngagementTurn::Kind::Speak);
    EXPECT_EQ(blank.text, "  ");
    EXPECT_EQ(blank.unspokenChars, 0U);
}

TEST(EngagementTurnTest, LeaveTopicKeepsReasonAndFinalText)
{
    const EngagementTurn leave = parseEngagementTurn(withToolCalls(
        {{"c1", "leave_topic", R"({"reason":"话题翻页了","final_text":"先这样"})"}}));
    EXPECT_EQ(leave.kind, EngagementTurn::Kind::Leave);
    EXPECT_EQ(leave.reason, "话题翻页了");
    EXPECT_EQ(leave.text, "先这样");

    // final_text 为空串或缺省：照样离场，只是不留最后一句
    const EngagementTurn empty = parseEngagementTurn(
        withToolCalls({{"c1", "leave_topic", R"({"reason":"聊完了","final_text":""})"}}));
    EXPECT_EQ(empty.kind, EngagementTurn::Kind::Leave);
    EXPECT_EQ(empty.reason, "聊完了");
    EXPECT_TRUE(empty.text.empty());
    const EngagementTurn omitted =
        parseEngagementTurn(withToolCalls({{"c1", "leave_topic", R"({"reason":"聊完了"})"}}));
    EXPECT_EQ(omitted.kind, EngagementTurn::Kind::Leave);
    EXPECT_TRUE(omitted.text.empty());
    EXPECT_EQ(omitted.unspokenChars, 0U);
    EXPECT_FALSE(omitted.stepBack) << "不填 step_back = 只结束这次跟进";
}

// 被请走（ADR-024）：step_back=true 才是暂时退开；非布尔值不算
TEST(EngagementTurnTest, LeaveTopicCarriesStepBack)
{
    const EngagementTurn stepBack = parseEngagementTurn(withToolCalls(
        {{"c1", "leave_topic", R"({"reason":"被请安静","final_text":"好","step_back":true})"}}));
    EXPECT_EQ(stepBack.kind, EngagementTurn::Kind::Leave);
    EXPECT_TRUE(stepBack.stepBack);
    EXPECT_EQ(stepBack.text, "好");

    const EngagementTurn explicitFalse = parseEngagementTurn(
        withToolCalls({{"c1", "leave_topic", R"({"reason":"聊完了","step_back":false})"}}));
    EXPECT_FALSE(explicitFalse.stepBack);
    const EngagementTurn notBoolean = parseEngagementTurn(
        withToolCalls({{"c1", "leave_topic", R"({"reason":"聊完了","step_back":"yes"})"}}));
    EXPECT_FALSE(notBoolean.stepBack);

    const EngagementTurn say =
        parseEngagementTurn(withToolCalls({{"c1", "group_say", R"({"text":"在","step_back":true})"}}));
    EXPECT_FALSE(say.stepBack) << "只有离场带 step_back";
}

// 同一回复里夹着别的调用：跳过非定案调用，按调用顺序取第一个定案动词
TEST(EngagementTurnTest, FirstVerbInCallOrderDecides)
{
    const EngagementTurn turn = parseEngagementTurn(withToolCalls(
        {{"c1", "recall_context", R"({"keywords":["船新","版本"]})"},
         {"c2", "leave_topic", R"({"reason":"翻页了","final_text":"撤了"})"},
         {"c3", "group_say", R"({"text":"不该发出"})"}}));
    EXPECT_EQ(turn.kind, EngagementTurn::Kind::Leave);
    EXPECT_EQ(turn.text, "撤了");
}

// 只剩未知/未开放工具（或工具轮到顶仍在要工具）：这一拍沉默，同带的 content 也不算话
TEST(EngagementTurnTest, OnlyUnknownToolsIsSilence)
{
    const EngagementTurn turn = parseEngagementTurn(
        withToolCalls({{"c1", "admin_tool", "{}"}, {"c2", "inspect_image", R"({"question":"看看"})"}},
                      "我先看看图"));
    EXPECT_EQ(turn.kind, EngagementTurn::Kind::Pass);
    EXPECT_TRUE(turn.text.empty());
    EXPECT_TRUE(turn.reason.empty());
    EXPECT_EQ(turn.unspokenChars, 0U) << "长度只在没有任何工具调用时填写";
}

TEST(EngagementTurnTest, NoToolsAndNoContentIsSilence)
{
    const EngagementTurn empty = parseEngagementTurn(textOnly(""));
    EXPECT_EQ(empty.kind, EngagementTurn::Kind::Pass);
    EXPECT_TRUE(empty.text.empty());
    EXPECT_EQ(empty.unspokenChars, 0U);

    const EngagementTurn blank = parseEngagementTurn(textOnly(" \n\t "));
    EXPECT_EQ(blank.kind, EngagementTurn::Kind::Pass);
    EXPECT_EQ(blank.unspokenChars, 0U) << "只有空白不算写了文字";
}

// L3 护栏：没调工具、直接写出的文字一律沉默（此前非空即当成发言发进群）。
// 判定不带出原文，只给去首尾空白后的码点数
TEST(EngagementTurnTest, BareTextIsSilenceAndOnlyItsLengthIsKept)
{
    const EngagementTurn narration = parseEngagementTurn(textOnly("（没有说话，继续留意后续）"));
    EXPECT_EQ(narration.kind, EngagementTurn::Kind::Pass);
    EXPECT_TRUE(narration.text.empty()) << "原文不带出判定";
    EXPECT_TRUE(narration.reason.empty());
    EXPECT_EQ(narration.unspokenChars, 13U);

    // 看起来像一句正常的话也一样：没经 group_say 就不是她说出口的话
    const EngagementTurn plain = parseEngagementTurn(textOnly("  ok，等等\n"));
    EXPECT_EQ(plain.kind, EngagementTurn::Kind::Pass);
    EXPECT_TRUE(plain.text.empty());
    EXPECT_EQ(plain.unspokenChars, 5U) << "按码点计，不是字节数（11）";
}

// 工具优先：group_say 同时带 content 时，说出口的只有 text
TEST(EngagementTurnTest, GroupSayWinsOverContent)
{
    const EngagementTurn turn = parseEngagementTurn(
        withToolCalls({{"c1", "group_say", R"({"text":"在的"})"}}, "（想了想，决定回一句）"));
    EXPECT_EQ(turn.kind, EngagementTurn::Kind::Speak);
    EXPECT_EQ(turn.text, "在的");
    EXPECT_EQ(turn.unspokenChars, 0U);
}
} // namespace
