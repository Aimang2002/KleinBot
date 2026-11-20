#include <gtest/gtest.h>

#include "Affect/BehaviorPlan.h"
#include "Affect/PlanTask.h"
#include "Affect/ReplyCheck.h"

#include <optional>
#include <string>
#include <vector>

// 经历层 E1（ADR-027）：结构核对（后核）与本轮任务句（再说）的纯函数

namespace
{
using affect::LengthTier;
using affect::ReplyCheck;

std::string repeat(const std::string &unit, int times)
{
    std::string text;
    for (int index = 0; index < times; ++index)
        text += unit;
    return text;
}

affect::BehaviorPlan v2Plan(const std::string &action, const std::string &move,
                            const std::string &length)
{
    affect::BehaviorPlan plan;
    plan.schemaVersion = affect::BehaviorPlan::kSchemaVersionV2;
    plan.action = action;
    plan.move = move;
    plan.length = length;
    plan.occasion = "群友在聊音色";
    plan.toolPolicy = action == "no_reply" ? "none" : "inherit";
    return plan;
}
} // namespace

// ---- 档位与计量 ----

TEST(ReplyCheckTest, TiersAreCodeConstants)
{
    EXPECT_TRUE(affect::lengthTierOf("short") == LengthTier::Short);
    EXPECT_TRUE(affect::lengthTierOf("one") == LengthTier::One);
    EXPECT_TRUE(affect::lengthTierOf("few") == LengthTier::Few);
    EXPECT_FALSE(affect::lengthTierOf("").has_value());
    EXPECT_FALSE(affect::lengthTierOf("long").has_value());
    EXPECT_TRUE(affect::lengthTierOr("") == LengthTier::Few) << "没给档时用最宽的一档";

    EXPECT_EQ(affect::maxSentencesOf(LengthTier::Short), 1U);
    EXPECT_EQ(affect::maxSentencesOf(LengthTier::One), 1U);
    EXPECT_EQ(affect::maxSentencesOf(LengthTier::Few), 3U);
    EXPECT_EQ(affect::maxCodepointsOf(LengthTier::Short), 24U);
    EXPECT_EQ(affect::maxCodepointsOf(LengthTier::One), 60U);
    EXPECT_EQ(affect::maxCodepointsOf(LengthTier::Few), 150U);
}

TEST(ReplyCheckTest, SentenceCountIsStructural)
{
    EXPECT_EQ(affect::sentenceCount("好"), 1U);
    EXPECT_EQ(affect::sentenceCount("哈哈哈！！！"), 1U) << "连着的句末标点算一处";
    EXPECT_EQ(affect::sentenceCount("其实…也还行，凑合吧"), 1U) << "省略号、逗号不断句";
    EXPECT_EQ(affect::sentenceCount("好的。那就这样吧！你呢？"), 3U);
    EXPECT_EQ(affect::sentenceCount("他说“好。”然后走了"), 2U);
    EXPECT_EQ(affect::sentenceCount("第一行\n第二行"), 2U) << "换行即分条，也是一句";
    EXPECT_EQ(affect::sentenceCount("……"), 0U) << "只有标点不算一句";
    EXPECT_EQ(affect::sentenceCount("  ok?  "), 1U);
    EXPECT_EQ(affect::codepointCount("  普鸭的挺好听的 "), 7U);
}

// ---- 核对的每个分支 ----

TEST(ReplyCheckTest, EmptyTextIsDropped)
{
    for (const std::string text : {"", "   ", "\n\t"})
    {
        const ReplyCheck check = affect::checkReply(text, LengthTier::One);
        EXPECT_EQ(check.verdict, ReplyCheck::Verdict::Drop);
        EXPECT_EQ(check.problems, std::vector<std::string>{"empty"});
    }
}

TEST(ReplyCheckTest, ProgramErrorAndPromptMarkersAreDropped)
{
    for (const std::string text : {"系统提示：模型无返回内容！", "  系统提示：处理超时，请重试。",
                                   "好的 [系统注] 以上是群聊现场", "[服务契约] 你首先是部署者的助手",
                                   "[关于你的事实] 由部署者填写"})
    {
        const ReplyCheck check = affect::checkReply(text, LengthTier::Few);
        EXPECT_EQ(check.verdict, ReplyCheck::Verdict::Drop) << text;
        EXPECT_EQ(check.problems, std::vector<std::string>{"system_text"}) << text;
    }
    EXPECT_FALSE(affect::looksLikeSystemText("这是系统提示吗？")) << "只认开头的程序前缀";
}

TEST(ReplyCheckTest, SentenceCountPerTier)
{
    const std::string two = "好的。那就这样吧。";
    EXPECT_EQ(affect::checkReply(two, LengthTier::Short).problems,
              std::vector<std::string>{"sentences"});
    EXPECT_EQ(affect::checkReply(two, LengthTier::One).verdict, ReplyCheck::Verdict::Retry);
    EXPECT_EQ(affect::checkReply(two, LengthTier::Few).verdict, ReplyCheck::Verdict::Send);

    const std::string four = "一。二。三。四。";
    const ReplyCheck check = affect::checkReply(four, LengthTier::Few);
    EXPECT_EQ(check.verdict, ReplyCheck::Verdict::Retry);
    EXPECT_EQ(check.sentences, 4U);
    EXPECT_EQ(affect::checkReply("一。二。三。", LengthTier::Few).verdict, ReplyCheck::Verdict::Send);
}

TEST(ReplyCheckTest, CodepointGuardPerTier)
{
    const std::vector<std::pair<LengthTier, int>> guards{
        {LengthTier::Short, 24}, {LengthTier::One, 60}, {LengthTier::Few, 150}};
    for (const auto &[tier, limit] : guards)
    {
        EXPECT_EQ(affect::checkReply(repeat("字", limit), tier).verdict, ReplyCheck::Verdict::Send)
            << limit;
        const ReplyCheck over = affect::checkReply(repeat("字", limit + 1), tier);
        EXPECT_EQ(over.verdict, ReplyCheck::Verdict::Retry) << limit;
        EXPECT_EQ(over.problems, std::vector<std::string>{"length"}) << limit;
        EXPECT_EQ(over.codepoints, static_cast<std::size_t>(limit + 1));
    }
}

TEST(ReplyCheckTest, MarkdownMarkersAskForRetry)
{
    for (const std::string text : {"**好**", "`code`", "# 标题", "- 一项", "> 引用", "看这个 [链接](https://a.b)",
                                   "__粗__"})
    {
        const ReplyCheck check = affect::checkReply(text, LengthTier::Few);
        EXPECT_EQ(check.verdict, ReplyCheck::Verdict::Retry) << text;
        EXPECT_EQ(check.problems, std::vector<std::string>{"markdown"}) << text;
    }
    for (const std::string text : {"#好", "1. 先这样", "2*3=6", "-_-", "C#", "好耶~~", "~~删掉~~"})
        EXPECT_FALSE(affect::hasMarkdown(text)) << text;
}

// “~~”是语气尾巴，不是删除线：照常发出，文字不动
TEST(ReplyCheckTest, TildeTailIsNotMarkdown)
{
    const ReplyCheck check = affect::checkReply("好耶~~", LengthTier::Short);
    EXPECT_EQ(check.verdict, ReplyCheck::Verdict::Send);
    EXPECT_TRUE(check.problems.empty());
    const affect::SettledReply settled = affect::settleReply("好耶~~", LengthTier::Short);
    EXPECT_TRUE(settled.send);
    EXPECT_EQ(settled.text, "好耶~~");
    EXPECT_EQ(settled.deviation, "");
}

TEST(ReplyCheckTest, StripMarkdownKeepsTheWords)
{
    EXPECT_EQ(affect::stripMarkdown("**好**呀"), "好呀");
    EXPECT_EQ(affect::stripMarkdown("# 标题\n- 一项\n> 引用"), "标题\n一项\n引用");
    EXPECT_EQ(affect::stripMarkdown("```cpp\nint x;\n```"), "int x;");
    EXPECT_EQ(affect::stripMarkdown("看 [这个](https://a.b) 吧"), "看 这个 https://a.b 吧");
    EXPECT_EQ(affect::stripMarkdown("用 `ls` 看"), "用 ls 看");
    EXPECT_EQ(affect::stripMarkdown("> - **嗯** 好耶~~"), "嗯 好耶~~") << "~~ 不是记号，不去掉";
    EXPECT_FALSE(affect::hasMarkdown(affect::stripMarkdown("> - **嗯** 好耶~~")));
}

TEST(ReplyCheckTest, RetryNoteSaysWhatWasWrong)
{
    const ReplyCheck check = affect::checkReply("好的。**那就这样吧**", LengthTier::One);
    ASSERT_EQ(check.problems, (std::vector<std::string>{"sentences", "markdown"}));
    EXPECT_EQ(affect::retryNote(check),
              "刚才那句没有发出：有 2 句，这一轮最多 1 句；带了 Markdown 记号，群聊里会原样显示。"
              "照本轮任务重说一次。");
    const ReplyCheck longOne = affect::checkReply(repeat("字", 30), LengthTier::Short);
    EXPECT_EQ(affect::retryNote(longOne, repeat("字", 30)),
              "你刚才要说的「" + repeat("字", 30) +
                  "」没有发出：有 30 个字，这一轮最多 24 个字。照本轮任务重说一次。");
}

// ---- 重说一次，然后照样发出并记偏差 ----

TEST(ReplyCheckTest, SettleSendsCountAndLengthAnywayWithDeviation)
{
    const affect::SettledReply longer = affect::settleReply(repeat("字", 70), LengthTier::One);
    EXPECT_TRUE(longer.send);
    EXPECT_EQ(longer.text, repeat("字", 70));
    EXPECT_EQ(longer.deviation, "reply_check:length");

    const affect::SettledReply both = affect::settleReply("一。二。**三**。", LengthTier::Short);
    EXPECT_TRUE(both.send);
    EXPECT_EQ(both.text, "一。二。三。");
    EXPECT_EQ(both.deviation, "reply_check:sentences,markdown_stripped");

    const affect::SettledReply dropped = affect::settleReply("系统提示：处理超时，请重试。",
                                                             LengthTier::One);
    EXPECT_FALSE(dropped.send);
    EXPECT_EQ(dropped.deviation, "reply_check:system_text");

    const affect::SettledReply fine = affect::settleReply("嗯嗯", LengthTier::Short);
    EXPECT_TRUE(fine.send);
    EXPECT_EQ(fine.deviation, "");
}

TEST(ReplyCheckTest, CheckWithRetryAsksExactlyOnce)
{
    int calls = 0;
    std::string seenNote;
    const auto retryWith = [&calls, &seenNote](std::optional<std::string> second)
    {
        return [&calls, &seenNote, second](const std::string &note) -> std::optional<std::string>
        {
            ++calls;
            seenNote = note;
            return second;
        };
    };

    // 合格：不重说
    affect::SettledReply settled = affect::checkWithRetry("好呀", LengthTier::One, retryWith("不该用到"));
    EXPECT_EQ(calls, 0);
    EXPECT_TRUE(settled.send);
    EXPECT_EQ(settled.text, "好呀");

    // 不发的：不重说
    settled = affect::checkWithRetry("", LengthTier::One, retryWith("不该用到"));
    EXPECT_EQ(calls, 0);
    EXPECT_FALSE(settled.send);
    EXPECT_EQ(settled.deviation, "reply_check:empty");

    // 重说后合格：发第二次的，无偏差
    settled = affect::checkWithRetry(repeat("字", 30), LengthTier::Short, retryWith("短了"));
    EXPECT_EQ(calls, 1);
    EXPECT_NE(seenNote.find("有 30 个字，这一轮最多 24 个字"), std::string::npos) << seenNote;
    EXPECT_TRUE(settled.send);
    EXPECT_EQ(settled.text, "短了");
    EXPECT_EQ(settled.deviation, "");

    // 重说后仍超：照样发出第二次的，记偏差
    settled = affect::checkWithRetry(repeat("字", 30), LengthTier::Short, retryWith(repeat("长", 40)));
    EXPECT_EQ(calls, 2);
    EXPECT_TRUE(settled.send);
    EXPECT_EQ(settled.text, repeat("长", 40));
    EXPECT_EQ(settled.deviation, "reply_check:length");

    // 重说失败：按第一次的文字下结论（Markdown 去掉记号后发出）
    settled = affect::checkWithRetry("**好**", LengthTier::Short, retryWith(std::nullopt));
    EXPECT_EQ(calls, 3);
    EXPECT_TRUE(settled.send);
    EXPECT_EQ(settled.text, "好");
    EXPECT_EQ(settled.deviation, "reply_check:markdown_stripped");

    // 重说成了程序提示：不发
    settled = affect::checkWithRetry(repeat("字", 30), LengthTier::Short,
                                     retryWith(std::string("系统提示：模型无返回内容！")));
    EXPECT_FALSE(settled.send);
    EXPECT_EQ(settled.deviation, "reply_check:system_text");
}

// ---- 本轮任务句（再说）----

TEST(PlanTaskTest, SentenceFromPlanAndTarget)
{
    const affect::ReplyCandidate target{"m-1", "老余", "「普鸭的挺好听的」", "普鸭的挺好听的"};
    EXPECT_EQ(affect::planTaskSentence(v2Plan("group_say", "follow", "one"), &target),
              "回应老余的那句「普鸭的挺好听的」；这一句是随口接话；一句以内。");
    EXPECT_EQ(affect::planTaskSentence(v2Plan("reply", "answer", "few"), nullptr),
              "这一句是回答；两三句以内。");
    EXPECT_EQ(affect::planTaskSentence(v2Plan("group_say", "ack", "short"), nullptr),
              "这一句是应一声；一短句。");

    const affect::ReplyCandidate picture{"m-2", "阿青", "一张图片", "[图片:abc]"};
    EXPECT_EQ(affect::planTaskSentence(v2Plan("group_say", "tease", "short"), &picture),
              "回应阿青发的一张图片；这一句是调侃；一短句。");
    const affect::ReplyCandidate anonymous{"m-3", "", "「在吗」", "在吗"};
    EXPECT_EQ(affect::planTaskSentence(v2Plan("reply", "answer", "one"), &anonymous),
              "回应对方的那句「在吗」；这一句是回答；一句以内。");

    EXPECT_EQ(affect::planTaskSentence(v2Plan("leave_topic", "", "short"), nullptr),
              "这一轮打算离场，用 leave_topic；要留最后一句的话，一短句。");
    EXPECT_EQ(affect::planTaskSentence(v2Plan("leave_topic", "", ""), nullptr),
              "这一轮打算离场，用 leave_topic。");
    EXPECT_EQ(affect::planTaskSentence(v2Plan("no_reply", "", ""), nullptr), "");

    affect::BehaviorPlan v1;
    v1.action = "group_say";
    EXPECT_EQ(affect::planTaskSentence(v1, &target), "") << "v1 计划没有任务句";
}

TEST(PlanTaskTest, LabelsCoverTheClosedSets)
{
    for (const std::string move : {"answer", "follow", "agree", "comfort", "tease", "ask", "ack"})
    {
        EXPECT_TRUE(affect::isKnownMove(move)) << move;
        EXPECT_FALSE(affect::moveLabel(move).empty()) << move;
    }
    EXPECT_FALSE(affect::isKnownMove("banter"));
    EXPECT_EQ(affect::moveLabel("banter"), "");
    for (const std::string length : {"short", "one", "few"})
        EXPECT_FALSE(affect::lengthLabel(length).empty()) << length;

    const std::vector<affect::ReplyCandidate> candidates{{"1", "a", "「x」", "x"}, {"2", "b", "「y」", "y"}};
    EXPECT_EQ(affect::findCandidate(candidates, std::string("2")), &candidates[1]);
    EXPECT_EQ(affect::findCandidate(candidates, std::string("3")), nullptr);
    EXPECT_EQ(affect::findCandidate(candidates, std::nullopt), nullptr);
}
