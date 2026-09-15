#include <gtest/gtest.h>

#include "Perception/EngagementService.h"
#include "Port/OutboundDelivery.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-engage-XXXXXX";
        pattern.push_back('\0');
        char *created = mkdtemp(pattern.data());
        if (created != nullptr)
            directory = created;
    }

    ~TemporaryDirectory()
    {
        if (!directory.empty())
            std::filesystem::remove_all(directory);
    }

    const std::string &path() const { return directory; }

private:
    std::string directory;
};

class FakeSender : public MessageSenderPort
{
public:
    void deliver(OutboundDelivery delivery) override
    {
        texts.push_back(std::get<TextMessage>(delivery.message).content);
    }
    std::vector<std::string> texts;
};

GroupMessageRecord record(std::uint64_t group, std::uint64_t qq, const std::string &nickname,
                          const std::string &text, std::int64_t ts,
                          bool mentionsBot = false, bool isSelf = false)
{
    GroupMessageRecord value;
    value.groupId = group;
    value.userId = qq;
    value.nickname = nickname;
    value.text = text;
    value.timestamp = ts;
    value.mentionsBot = mentionsBot;
    value.isSelf = isSelf;
    return value;
}

ChatResponse sayResponse(const std::string &text)
{
    ChatResponse response;
    response.code = 200;
    response.finish_reason = "tool_calls";
    response.tool_calls.push_back({"call-1", "group_say",
                                   R"({"text":")" + text + R"("})"});
    return response;
}

ChatResponse passResponse()
{
    ChatResponse response;
    response.code = 200;
    response.finish_reason = "stop";
    return response;
}

ChatResponse leaveResponse(const std::string &reason, const std::string &finalText = {})
{
    ChatResponse response;
    response.code = 200;
    response.finish_reason = "tool_calls";
    response.tool_calls.push_back(
        {"call-1", "leave_topic",
         R"({"reason":")" + reason + R"(", "final_text":")" + finalText + R"("})"});
    return response;
}

struct Harness
{
    TemporaryDirectory directory;
    std::unique_ptr<GroupContextStore> store;
    std::unique_ptr<FakeSender> sender;
    std::unique_ptr<EngagementService> service;
    std::int64_t now = 1'000'000;
    std::vector<std::uint64_t> submittedTurns;
    std::vector<std::uint64_t> submittedJudges;
    std::vector<ChatResponse> turnResponses; // 依次弹出；空则默认 pass

    explicit Harness(BotIdentity bot = {987654321, 123456789, "Klein"})
        : store(std::make_unique<GroupContextStore>(directory.path() + "/conversation.db")),
          sender(std::make_unique<FakeSender>()),
          service(std::make_unique<EngagementService>(
              bot, store.get(), sender.get(), [this] { return now; }))
    {
        service->setSubmitTurn([this](std::uint64_t group) { submittedTurns.push_back(group); });
        service->setSubmitJudge([this](std::uint64_t group) { submittedJudges.push_back(group); });
        service->setMainAgent([this](const std::string &, const std::vector<ChatMessage> &,
                                     const std::vector<std::string> &)
                              {
                                  if (turnResponses.empty())
                                      return passResponse();
                                  ChatResponse response = turnResponses.front();
                                  turnResponses.erase(turnResponses.begin());
                                  return response;
                              });
        service->setWorker([](const std::string &, const std::string &)
                           { return std::string{}; });
    }

    void feed(std::uint64_t group, const std::string &text, std::int64_t ts,
              bool mentionsBot = false, bool atMentioned = false)
    {
        GroupMessageRecord value = record(group, 42, "小白", text, ts, mentionsBot);
        store->append(value);
        service->onGroupMessage(value, atMentioned);
    }
};

BotIdentity botIdentity()
{
    BotIdentity bot;
    bot.id = 987654321;
    bot.managerId = 123456789;
    bot.name = "Klein";
    return bot;
}
} // namespace

TEST(EngagementTurnTest, ParsesToolCallsAndPlainText)
{
    auto say = EngagementService::parseTurn(sayResponse("大家好"));
    EXPECT_EQ(say.kind, EngagementTurn::Kind::Speak);
    EXPECT_EQ(say.text, "大家好");

    auto leave = EngagementService::parseTurn(leaveResponse("话题翻页了", "先这样"));
    EXPECT_EQ(leave.kind, EngagementTurn::Kind::Leave);
    EXPECT_EQ(leave.reason, "话题翻页了");
    EXPECT_EQ(leave.text, "先这样");

    auto pass = EngagementService::parseTurn(passResponse());
    EXPECT_EQ(pass.kind, EngagementTurn::Kind::Pass);

    // 无工具的裸文本宽容视为说话
    ChatResponse bare;
    bare.code = 200;
    bare.content = "那我说一句";
    EXPECT_EQ(EngagementService::parseTurn(bare).kind, EngagementTurn::Kind::Speak);

    // 空白裸文本 = 沉默
    ChatResponse blank;
    blank.code = 200;
    blank.content = "  ";
    EXPECT_EQ(EngagementService::parseTurn(blank).kind, EngagementTurn::Kind::Pass);
}

TEST(EngagementSessionTest, AtActivationMergesIntoLiveSession)
{
    Harness harness;
    harness.service->onAtActivated(8823, "周末去哪玩");
    auto session = harness.service->sessionOf(8823);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->state, EngagementState::Active);
    EXPECT_EQ(session->topicLine, "周末去哪玩");
    EXPECT_FALSE(session->entryPending) << "@ 激活无入场轮";

    // 会话存活期间的再次 @：并入会话（不重置轮次计数，防硬后盖被绕过）
    harness.now += 60;
    harness.service->onAtActivated(8823, "换个话题");
    auto merged = harness.service->sessionOf(8823);
    ASSERT_TRUE(merged.has_value());
    EXPECT_EQ(merged->topicLine, "周末去哪玩") << "不重建会话";
    EXPECT_EQ(merged->startTs, 1'000'000);
    EXPECT_EQ(merged->lastActivityTs, harness.now);
}

TEST(EngagementSessionTest, EndedSessionRetainedThenSweptAfterRetention)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题A");
    harness.feed(8823, "现场消息", harness.now);
    harness.turnResponses.push_back(leaveResponse("聊完了"));
    harness.service->runTurn(8823);
    auto ended = harness.service->sessionOf(8823);
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->state, EngagementState::Ended);
    EXPECT_EQ(ended->endReason, "聊完了");

    // 保留期内不清理
    harness.now += 23 * 60 * 60;
    harness.service->pump(harness.now);
    EXPECT_TRUE(harness.service->sessionOf(8823).has_value());

    // 保留期满：pump 清扫
    harness.now += 2 * 60 * 60;
    harness.service->pump(harness.now);
    EXPECT_FALSE(harness.service->sessionOf(8823).has_value());
}

TEST(EngagementSessionTest, NewActivationReplacesEndedSessionImmediately)
{
    Harness harness;
    harness.service->onAtActivated(8823, "早上的话题");
    harness.feed(8823, "现场消息", harness.now);
    harness.turnResponses.push_back(leaveResponse("结束"));
    harness.service->runTurn(8823);
    ASSERT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Ended);

    // 保留期内下午再触发：旧上下文立刻让位
    harness.now += 5 * 60 * 60;
    harness.service->onAtActivated(8823, "下午的新话题");
    auto session = harness.service->sessionOf(8823);
    ASSERT_EQ(session->state, EngagementState::Active);
    EXPECT_EQ(session->topicLine, "下午的新话题");
}

TEST(EngagementSessionTest, TurnSpeakDeliversRecordsAndCounts)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "第一句", harness.now);
    harness.feed(8823, "第二句", harness.now + 1);
    harness.feed(8823, "第三句", harness.now + 2);
    harness.turnResponses.push_back(sayResponse("我来插一句"));
    harness.service->runTurn(8823);

    ASSERT_EQ(harness.sender->texts.size(), 1U);
    EXPECT_EQ(harness.sender->texts.front(), "我来插一句");
    auto session = harness.service->sessionOf(8823);
    EXPECT_EQ(session->turnCount, 1);
    EXPECT_EQ(session->consecutivePasses, 0);
    // 她的发言也进了群内容库（isSelf）
    bool foundSelf = false;
    for (const GroupMessageRecord &entry : harness.store->snapshot(8823))
        foundSelf = foundSelf || (entry.isSelf && entry.text == "我来插一句");
    EXPECT_TRUE(foundSelf);
}

TEST(EngagementSessionTest, ThreeConsecutivePassesEndSession)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);
    harness.turnResponses.push_back(passResponse());
    harness.turnResponses.push_back(passResponse());
    harness.turnResponses.push_back(passResponse());
    harness.service->runTurn(8823);
    harness.service->runTurn(8823);
    EXPECT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Active);
    harness.service->runTurn(8823);
    EXPECT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Ended);
    EXPECT_EQ(harness.service->sessionOf(8823)->endReason, "连续沉默");
}

TEST(EngagementSessionTest, LeaveWithFinalTextDeliversThenEnds)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);
    harness.turnResponses.push_back(leaveResponse("话题翻页了", "那我先潜水了"));
    harness.service->runTurn(8823);
    ASSERT_EQ(harness.sender->texts.size(), 1U);
    EXPECT_EQ(harness.sender->texts.front(), "那我先潜水了");
    EXPECT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Ended);
}

TEST(EngagementSessionTest, TurnCountHardCapForcesEndWithoutModelCall)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);
    for (int index = 0; index < 20; ++index)
    {
        harness.turnResponses.push_back(sayResponse("第" + std::to_string(index) + "句"));
        harness.service->runTurn(8823);
    }
    EXPECT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Active);

    // 第 21 轮：硬后盖直接结束，不再调模型（push 的响应原样留下未被消费）
    const std::size_t callsBefore = harness.turnResponses.size();
    harness.turnResponses.push_back(sayResponse("多余的一句"));
    harness.service->runTurn(8823);
    EXPECT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Ended);
    ASSERT_EQ(harness.sender->texts.size(), 20U) << "超限轮不发言";
    EXPECT_EQ(harness.turnResponses.size(), callsBefore + 1) << "超限轮不消费模型响应";
}

TEST(EngagementSessionTest, PumpEnforcesDurationAndInactivityBackstops)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");

    harness.now += 31 * 60;
    harness.service->pump(harness.now);
    EXPECT_EQ(harness.service->sessionOf(8823)->endReason, "forced: 跟进时长达上限");

    harness.service->onAtActivated(8824, "另一个群");
    harness.now += 11 * 60;
    harness.service->pump(harness.now);
    EXPECT_EQ(harness.service->sessionOf(8824)->endReason, "话题沉寂");
}

TEST(EngagementSessionTest, LullAccumulatedMessagesTriggerTurn)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "一", harness.now);
    harness.feed(8823, "二", harness.now + 1);
    harness.feed(8823, "三", harness.now + 2);
    harness.now += 60; // 跨过 45s 轮次节流 + 10s lull（最后消息在 now+2）
    harness.service->pump(harness.now);
    ASSERT_FALSE(harness.submittedTurns.empty());
    EXPECT_EQ(harness.submittedTurns.back(), 8823U);
}

TEST(EngagementSessionTest, AddressPendingTriggersImmediateTurn)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    const std::size_t before = harness.submittedTurns.size();
    harness.feed(8823, "Klein 你觉得呢", harness.now + 5, true);
    ASSERT_EQ(harness.submittedTurns.size(), before + 1);
    EXPECT_TRUE(harness.service->sessionOf(8823)->addressPending);
}

TEST(EngagementSessionTest, JudgeFuseLimitsRate)
{
    Harness harness;
    harness.service->setWorker([](const std::string &, const std::string &)
                               { return std::string("NO：理由"); });
    // 最小间隔：60s 内的连续提及只提交一次（runJudge 清合流标记模拟 lane 执行）
    harness.feed(8823, "Klein 在吗", harness.now, true);
    ASSERT_EQ(harness.submittedJudges.size(), 1U);
    harness.service->runJudge(8823);
    harness.feed(8823, "Klein 呢", harness.now + 10, true);
    EXPECT_EQ(harness.submittedJudges.size(), 1U) << "60s 内被最小间隔拦下";
    harness.service->runJudge(8823);
    harness.now += 61;
    harness.feed(8823, "Klein 帮我看看", harness.now, true);
    EXPECT_EQ(harness.submittedJudges.size(), 2U);
}

TEST(EngagementSessionTest, JudgeYesActivatesSessionWithEntryTurn)
{
    Harness harness;
    harness.service->setWorker([](const std::string &, const std::string &)
                               { return "YES"; });
    harness.feed(8823, "Klein 昨天那个问题解决了", harness.now, true);
    ASSERT_EQ(harness.submittedJudges.size(), 1U);
    harness.service->runJudge(8823);

    auto session = harness.service->sessionOf(8823);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->state, EngagementState::Active);
    EXPECT_TRUE(session->entryPending) << "入场轮待执行";
    EXPECT_TRUE(harness.submittedTurns.back() == 8823U);

    // 入场轮：主模型三选一，选了说话
    harness.turnResponses.push_back(sayResponse("解决了吗？怎么弄的"));
    harness.service->runTurn(8823);
    ASSERT_FALSE(harness.sender->texts.empty());
    EXPECT_EQ(harness.sender->texts.back(), "解决了吗？怎么弄的");
    EXPECT_FALSE(harness.service->sessionOf(8823)->entryPending) << "入场轮执行后标记清除";
}

TEST(EngagementSessionTest, JudgeNoLeavesGroupAlone)
{
    Harness harness;
    harness.service->setWorker([](const std::string &, const std::string &)
                               { return "NO：只是在聊游戏恰巧含这个词"; });
    harness.feed(8823, "Klein 这个英雄真弱", harness.now, true);
    ASSERT_EQ(harness.submittedJudges.size(), 1U);
    harness.service->runJudge(8823);
    EXPECT_FALSE(harness.service->sessionOf(8823).has_value());
}

TEST(EngagementSessionTest, ActiveSessionMergesMentionsInsteadOfReactivation)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    const std::size_t judgesBefore = harness.submittedJudges.size();
    // 会话存活期间提名字：并入会话（点名触发轮次），不再走 judge
    harness.feed(8823, "Klein 你怎么看", harness.now + 5, true);
    EXPECT_EQ(harness.submittedJudges.size(), judgesBefore);
    EXPECT_TRUE(harness.service->sessionOf(8823)->addressPending);
}

TEST(EngagementSessionTest, SelfActivityResetsPassCounterAndBeat)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);
    harness.turnResponses.push_back(passResponse());
    harness.service->runTurn(8823);
    EXPECT_EQ(harness.service->sessionOf(8823)->consecutivePasses, 1);

    // 她经正常路径发言（@ 回复）：重置节拍
    harness.service->onSelfActivity(8823, harness.now + 30);
    EXPECT_EQ(harness.service->sessionOf(8823)->consecutivePasses, 0);
}
