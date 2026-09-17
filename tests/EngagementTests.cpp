#include <gtest/gtest.h>

#include "Perception/EngagementService.h"
#include "Perception/GroupContextService.h"
#include "Perception/GroupListService.h"
#include "Network/OneBotApiChannel.h"
#include "Port/OutboundDelivery.h"

#include <chrono>

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
    std::vector<std::string> agentContracts; // 主 Agent 每次请求的 system 注记
    std::vector<std::vector<ChatMessage>> agentHistories;
    std::vector<std::string> workerSystems; // 杂务模型调用记录
    std::vector<std::string> workerUsers;

    explicit Harness(BotIdentity bot = {987654321, 123456789, "Klein"})
        : store(std::make_unique<GroupContextStore>(directory.path() + "/conversation.db")),
          sender(std::make_unique<FakeSender>()),
          service(std::make_unique<EngagementService>(
              bot, store.get(), sender.get(), [this] { return now; }))
    {
        service->setSubmitTurn([this](std::uint64_t group) { submittedTurns.push_back(group); });
        service->setSubmitJudge([this](std::uint64_t group) { submittedJudges.push_back(group); });
        service->setMainAgent(
            [this](const std::string &contract, const std::vector<ChatMessage> &history,
                   const std::vector<std::string> &)
            {
                agentContracts.push_back(contract);
                agentHistories.push_back(history);
                if (turnResponses.empty())
                    return passResponse();
                ChatResponse response = turnResponses.front();
                turnResponses.erase(turnResponses.begin());
                return response;
            });
        service->setWorker(
            [this](const std::string &system, const std::string &user)
            {
                workerSystems.push_back(system);
                workerUsers.push_back(user);
                return std::string{};
            });
    }

    void feed(std::uint64_t group, const std::string &text, std::int64_t ts,
              bool mentionsBot = false, bool atMentioned = false)
    {
        GroupMessageRecord value = record(group, 42, "小白", text, ts, mentionsBot);
        store->append(value);
        service->onGroupMessage(value, atMentioned);
    }

    void feedRaw(std::uint64_t group, const std::string &text, std::int64_t ts)
    {
        GroupMessageRecord value = record(group, 42, "小白", text, ts);
        store->append(value); // 只入库不喂会话（构造超窗口材料用）
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

std::string repeatUtf8(const std::string &unit, int times)
{
    std::string text;
    for (int index = 0; index < times; ++index)
        text += unit;
    return text;
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
    harness.now += 1 * 60 * 60;
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

    // 保留期内稍后再触发：旧上下文立刻让位
    harness.now += 1 * 60 * 60;
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

TEST(EngagementSessionTest, MessageCapForcesEndWithoutModelCall)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题"); // messagesSeen = 0

    // 299 条群消息，每条后跟一轮发言
    for (int index = 0; index < 299; ++index)
    {
        harness.feed(8823, "群聊" + std::to_string(index), harness.now + index);
        harness.turnResponses.push_back(sayResponse("第" + std::to_string(index) + "句"));
        harness.service->runTurn(8823);
    }
    ASSERT_EQ(harness.service->sessionOf(8823)->messagesSeen, 299);
    EXPECT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Active);
    EXPECT_EQ(harness.sender->texts.size(), 299U);

    // 第 300 条到量：pump 硬后盖强制收场，不再调模型
    harness.turnResponses.push_back(sayResponse("多余的一句"));
    harness.feed(8823, "压垮的最后一根稻草", harness.now + 299);
    harness.service->pump(harness.now + 299);
    EXPECT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Ended);
    EXPECT_EQ(harness.sender->texts.size(), 299U) << "超限不发言";
    ASSERT_EQ(harness.turnResponses.size(), 1U) << "超限不消费模型响应";
}


TEST(EngagementSessionTest, PumpEnforcesInactivityBackstop)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");

    harness.now += 11 * 60;
    harness.service->pump(harness.now);
    EXPECT_EQ(harness.service->sessionOf(8823)->endReason, "话题沉寂");
}


TEST(EngagementSessionTest, LullAccumulatedMessagesTriggerTurn)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "一", harness.now);
    harness.feed(8823, "二", harness.now + 1);
    harness.feed(8823, "三", harness.now + 2);
    harness.now += 60; // 跨过 20s 轮次节流 + 4s lull（最后消息在 now+2）
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

TEST(EngagementContextTest, EstimateTokensHeuristic)
{
    EXPECT_EQ(EngagementService::estimateTokens("abcd"), 1U);   // ASCII 4:1
    EXPECT_EQ(EngagementService::estimateTokens("一二三四"), 4U); // CJK 1:1
    EXPECT_EQ(EngagementService::estimateTokens("ab一二"), 2U);
}

TEST(EngagementContextTest, OversizedMessageTruncatedKeepingHead)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "正常消息", harness.now);
    harness.feed(8823, repeatUtf8("超导", 600), harness.now + 1); // 1200 token > 1000
    harness.turnResponses.push_back(sayResponse("收到"));
    harness.service->runTurn(8823);

    ASSERT_FALSE(harness.agentHistories.empty());
    const std::string &material = harness.agentHistories.front().front().content;
    EXPECT_NE(material.find("……（后文略）"), std::string::npos) << "截断有省略标记";
    EXPECT_NE(material.find(repeatUtf8("超导", 60)), std::string::npos)
        << "保头截断，开头语气仍在";
    EXPECT_EQ(material.find(repeatUtf8("超导", 600)), std::string::npos)
        << "原文全文不进材料";
    EXPECT_NE(material.find("正常消息"), std::string::npos);
}

TEST(EngagementContextTest, OverBudgetWindowCompressedToDigestPlusTail)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    // 10 条 × 350 个汉字 ≈ 3500 token > 3000 预算
    for (int index = 0; index < 10; ++index)
        harness.feedRaw(8823, "消息" + std::to_string(index) + repeatUtf8("讨论", 175),
                        harness.now + index);
    harness.service->setWorker([&harness](const std::string &system, const std::string &user)
                               {
                                   harness.workerSystems.push_back(system);
                                   harness.workerUsers.push_back(user);
                                   return std::string("合并后的摘要");
                               });
    harness.turnResponses.push_back(sayResponse("了解了"));
    harness.service->runTurn(8823);

    // 杂务模型收到压缩任务
    ASSERT_FALSE(harness.workerSystems.empty());
    EXPECT_NE(harness.workerSystems.front().find("压缩器"), std::string::npos);
    EXPECT_NE(harness.workerUsers.front().find("消息0"), std::string::npos);

    // 材料 = 摘要 + 尾巴原文（最后 3 条），被压缩的旧消息原文不出现
    const std::string &material = harness.agentHistories.front().front().content;
    EXPECT_NE(material.find("合并后的摘要"), std::string::npos);
    EXPECT_NE(material.find("消息9"), std::string::npos);
    EXPECT_EQ(material.find("消息0论"), std::string::npos) << "已压缩部分不再以原文出现";

    // 滚动水位落库到会话
    auto session = harness.service->sessionOf(8823);
    EXPECT_EQ(session->digest, "合并后的摘要");
    EXPECT_EQ(session->compressedUpToTs, harness.now + 6);
}

TEST(EngagementContextTest, RecallSearchesBeyondWindowOncePerSession)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    // 32 条：窗口只装最后 30 条，最早 2 条只能靠召回
    for (int index = 0; index < 32; ++index)
    {
        const std::string text = index < 2 ? "船新版本介绍" + std::to_string(index)
                                           : "闲聊" + std::to_string(index);
        harness.feedRaw(8823, text, harness.now + index);
    }

    ChatResponse recallCall;
    recallCall.code = 200;
    recallCall.finish_reason = "tool_calls";
    recallCall.tool_calls.push_back({"rc-1", "recall_context",
                                     R"({"keywords":["船新","版本"]})"});
    harness.turnResponses.push_back(recallCall);
    harness.turnResponses.push_back(sayResponse("是那个船新版本吗"));
    harness.service->runTurn(8823);

    // 第一次请求的材料里没有窗口外的消息；第二次请求带回了召回结果
    ASSERT_GE(harness.agentHistories.size(), 2U);
    const std::string &firstMaterial = harness.agentHistories[0].front().content;
    EXPECT_EQ(firstMaterial.find("船新版本介绍"), std::string::npos);
    ASSERT_GE(harness.agentHistories[1].size(), 3U);
    const std::string &toolResult = harness.agentHistories[1][2].content;
    EXPECT_NE(toolResult.find("船新版本介绍0"), std::string::npos);
    EXPECT_EQ(harness.service->sessionOf(8823)->recallUsed, 1);
    ASSERT_FALSE(harness.sender->texts.empty());
    EXPECT_EQ(harness.sender->texts.back(), "是那个船新版本吗");

    // 第二次召回：预算用尽，不再请求模型检索
    ChatResponse recallAgain;
    recallAgain.code = 200;
    recallAgain.finish_reason = "tool_calls";
    recallAgain.tool_calls.push_back({"rc-2", "recall_context",
                                      R"({"keywords":["船新"]})"});
    harness.turnResponses.push_back(recallAgain);
    harness.service->runTurn(8823);
    ASSERT_GE(harness.agentHistories.size(), 3U);
    // 预算用尽：schema 都不给，模型无从发起——但万一发起，回灌预算提示
    EXPECT_GT(harness.agentContracts.size(), 2U);
}

TEST(EngagementContextTest, RecallNoteReflectsBudgetInContract)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);
    harness.turnResponses.push_back(sayResponse("第一轮"));
    harness.service->runTurn(8823);
    EXPECT_NE(harness.agentContracts.front().find("还有 1 次上下文召回"), std::string::npos);
}

// ===== 集成：GroupContextService.observe → EngagementService 全链路 =====

namespace
{
class NoopEngageApiChannel final : public OneBotApiChannel
{
public:
    OneBotApiResult call(const std::string &, nlohmann::json, std::chrono::milliseconds) override
    {
        return {};
    }
};

InboundMessage inboundChat(std::uint64_t group, std::uint64_t user, const std::string &text,
                           std::int64_t ts, bool atBot = false)
{
    InboundMessage message;
    message.post_type = "message";
    message.message_type = "group";
    message.user_id = user;
    message.group_id = group;
    message.nickname = "小白";
    message.plain_text = text;
    message.message_timestamp = ts;
    message.message_id = 100000 + ts;
    message.message_id_raw = "m" + std::to_string(message.message_id);
    if (atBot)
        message.mentioned_ids.push_back(10086);
    return message;
}
} // namespace

TEST(EngagementIntegrationTest, ObserveFeedsEngagementAndJudgeActivates)
{
    TemporaryDirectory temporaryDirectory;
    GroupContextStore store(temporaryDirectory.path() + "/conversation.db");
    FakeSender sender;
    NoopEngageApiChannel api;
    GroupListService state(temporaryDirectory.path() + "/state.db", api);
    state.setFeatureEnabled(true);
    state.setMonitored(8823, true);

    BotIdentity bot;
    bot.id = 10086;
    bot.name = "Klein";
    GroupContextService contextService(&state, bot, &store, &sender);
    EngagementService engagement(bot, &store, &sender, [] { return std::int64_t(1000); });
    contextService.setEngagement(&engagement);
    engagement.setWorker([](const std::string &, const std::string &)
                         { return std::string("YES"); });
    std::vector<std::uint64_t> turns;
    engagement.setSubmitTurn([&](std::uint64_t group) { turns.push_back(group); });

    // 群里聊出语境，有人提名字无 @：observe 喂 observe→judge 提交 →
    // lane 内 runJudge 判 YES → 清旧建新 + 入场轮提交
    contextService.observe(inboundChat(8823, 10, "原神深渊十二层怎么打", 1000));
    contextService.observe(inboundChat(8823, 20, "深渊阵容带钟离稳一点", 1001));
    contextService.observe(inboundChat(8823, 30, "Klein 你深渊用的什么阵容", 1002));
    engagement.runJudge(8823); // 模拟群 lane 执行 judge 任务
    ASSERT_EQ(turns.size(), 1U);
    ASSERT_TRUE(engagement.sessionOf(8823).has_value());
    EXPECT_TRUE(engagement.sessionOf(8823)->entryPending);

    // 会话存活期间再提名字：并入会话（点名触发轮次），不再走 judge
    contextService.observe(inboundChat(8823, 40, "Klein 说得对", 1003));
    EXPECT_EQ(turns.size(), 2U) << "第二次是点名轮次，不是重新激活";
    EXPECT_TRUE(engagement.sessionOf(8823)->addressPending);

    // 非@群消息照常入库且喂活动（无新判定）
    contextService.observe(inboundChat(8824, 50, "别的群不监控", 1004));
    EXPECT_FALSE(engagement.sessionOf(8824).has_value());
}

TEST(EngagementIntegrationTest, AtMentionHardActivatesSession)
{
    TemporaryDirectory temporaryDirectory;
    GroupContextStore store(temporaryDirectory.path() + "/conversation.db");
    FakeSender sender;
    NoopEngageApiChannel api;
    GroupListService state(temporaryDirectory.path() + "/state.db", api);
    state.setFeatureEnabled(true);
    state.setMonitored(8823, true);

    BotIdentity bot;
    bot.id = 10086;
    bot.name = "Klein";
    GroupContextService contextService(&state, bot, &store, &sender);
    EngagementService engagement(bot, &store, &sender, [] { return std::int64_t(1000); });
    contextService.setEngagement(&engagement);

    // @ 消息：Message 路径调用 onAtActivated 硬激活（observe 只管入库）
    contextService.observe(inboundChat(8823, 10, "@Klein 来聊聊", 1000, true));
    EXPECT_FALSE(engagement.sessionOf(8823).has_value()) << "observe 不开会话";
    contextService.onAtActivated(8823, "@Klein 来聊聊");
    ASSERT_TRUE(engagement.sessionOf(8823).has_value());
    EXPECT_FALSE(engagement.sessionOf(8823)->entryPending) << "@ 激活无入场轮";
}


// ===== 冷启动自动介入：基线模型 + 热度尖峰 + 频控 =====
// 机制说明：冷启动不经过 worker judge——尖峰命中直接开冷启动会话并跑
// 入场轮（带人格的主模型三选一自决）。单测 Harness 的 submitTurn 只入队，
// 因此 spike() 里显式对新增提交逐个 runTurn（模拟群 lane 执行）。
struct ColdFlow
{
    Harness &h;
    std::int64_t &now;
    const std::uint64_t group = 77500;

    ColdFlow(Harness &harness) : h(harness), now(harness.now) {}

    void setMembers(long count)
    {
        h.service->setMemberProvider([count](std::uint64_t) { return count; });
    }

    // 低频日常流量：hours 小时、每小时 msgs 条（学习基线，跨整点采样 EMA）
    void warm(int hours, int msgsPerHour)
    {
        for (int hour = 0; hour < hours; ++hour)
        {
            for (int i = 0; i < msgsPerHour; ++i)
            {
                now += 3600 / msgsPerHour;
                h.feed(group, "闲聊" + std::to_string(hour * 10 + i), now);
            }
            now += 120;
        }
    }

    // 10 分钟内突发 count 条，随后跨过检查节流并执行排队的轮次
    void spike(int count, int step = 8)
    {
        const std::size_t before = h.submittedTurns.size();
        for (int i = 0; i < count; ++i)
        {
            now += step;
            h.feed(group, "话题聊爆了" + std::to_string(i), now);
        }
        now += 61;
        h.service->pump(now);
        for (std::size_t i = before; i < h.submittedTurns.size(); ++i)
            h.service->runTurn(h.submittedTurns[i]);
    }
};

TEST(EngagementColdTest, ArmedAfterTwoHoursThenSpikeTriggersColdEntry)
{
    Harness harness;
    ColdFlow flow(harness);
    flow.setMembers(200);

    // 武装期内尖峰不触发
    flow.spike(12);
    EXPECT_FALSE(harness.service->sessionOf(77500).has_value()) << "武装期未过不应介入";

    // 学习基线 3 小时（每小时 4 条 → 阈值 = max(6, 200×3%=6, 基线×3) = 6）
    flow.warm(3, 4);
    harness.turnResponses.push_back(sayResponse("聊什么呢 我也看看"));
    flow.spike(12);
    ASSERT_TRUE(harness.service->sessionOf(77500).has_value()) << "武装期满+尖峰应触发";
    EXPECT_TRUE(harness.service->sessionOf(77500)->coldInitiated);
    EXPECT_FALSE(harness.sender->texts.empty()) << "冷入场发言";
    EXPECT_NE(harness.agentHistories.back().front().content.find("没有任何人点名你"), std::string::npos)
        << "冷入场应使用人格自决 ask";
}

TEST(EngagementColdTest, BaselineLearnsAndRaisesThreshold)
{
    Harness harness;
    ColdFlow flow(harness);
    flow.setMembers(100); // 地板 = max(6, 100×3%=3) = 6

    // 每小时 40 条 → EMA ≈ 40 → 阈值 = max(6, 3, 40×24/144×3=20) = 20
    flow.warm(4, 40);
    flow.spike(12);
    EXPECT_FALSE(harness.service->sessionOf(77500).has_value())
        << "高基线群里的小突发不应触发";
    harness.turnResponses.push_back(sayResponse("这么热闹 我也来说两句"));
    flow.spike(25);
    EXPECT_TRUE(harness.service->sessionOf(77500).has_value()) << "超阈值真尖峰应触发";
}

TEST(EngagementColdTest, MemberFloorBlocksSmallBurstsInHugeGroups)
{
    Harness harness;
    ColdFlow flow(harness);
    flow.setMembers(2000); // 人数地板 = 2000×3% = 60

    flow.warm(3, 2); // 基线极低
    flow.spike(30);  // 超过 6 与基线×3，但低于人数地板
    EXPECT_FALSE(harness.service->sessionOf(77500).has_value())
        << "大群的人数地板应抬高触发门槛";
}

TEST(EngagementColdTest, DailyCapLimitsColdEvaluations)
{
    Harness harness;
    ColdFlow flow(harness);
    flow.setMembers(100);
    flow.warm(3, 4);

    // 3 次冷启动评估：默认 pass → 自决不加入 → 冷却 2h
    for (int round = 0; round < 3; ++round)
    {
        const std::size_t before = harness.agentHistories.size();
        flow.spike(12);
        ASSERT_GT(harness.agentHistories.size(), before) << "第 " << round + 1 << " 次应评估";
        flow.now += 2 * 60 * 60; // 跨过冷启动冷却
    }
    const std::size_t callsAfterCap = harness.agentHistories.size();

    // 第 4 次：当日帽拦截，不再发起评估
    flow.spike(12);
    EXPECT_EQ(harness.agentHistories.size(), callsAfterCap) << "当日帽应拦截第 4 次";
}

TEST(EngagementColdTest, ColdPassDecidesNotToJoinAndCooldownApplies)
{
    Harness harness;
    ColdFlow flow(harness);
    flow.setMembers(100);
    flow.warm(3, 4);

    // 冷入场轮模型默认 pass → 自决不加入 → 会话即收场
    flow.spike(12);
    auto session = harness.service->sessionOf(77500);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->state, EngagementState::Ended);
    EXPECT_NE(session->endReason.find("冷启动自决"), std::string::npos);

    // 冷却期（2 小时）内再尖峰不再评估
    const std::size_t before = harness.agentHistories.size();
    flow.now += 10 * 60;
    flow.spike(12);
    EXPECT_EQ(harness.agentHistories.size(), before) << "冷却期内不应再评估";
}

TEST(EngagementColdTest, LearningStateSurvivesRestart)
{
    TemporaryDirectory temporaryDirectory;
    const std::string dbPath = temporaryDirectory.path() + "/c.db";
    BotIdentity bot;
    bot.id = 987654321;
    bot.name = "Klein";
    const std::uint64_t group = 77501;

    std::int64_t now = 1'000'000;
    auto store1 = std::make_unique<GroupContextStore>(dbPath);
    EngagementService first(bot, store1.get(), nullptr, [&now] { return now; });
    first.setMemberProvider([](std::uint64_t) { return 100L; });

    // 学习基线 4 小时（每小时 40 条）
    for (int hour = 0; hour < 4; ++hour)
    {
        for (int i = 0; i < 40; ++i)
        {
            now += 900;
            auto rec = record(group, 42, "小白", "日常" + std::to_string(hour * 40 + i), now);
            store1->append(rec);
            first.onGroupMessage(rec, false);
        }
        now += 120;
    }
    store1.reset(); // "重启"

    // 重启后：武装期与基线从库恢复 → 立即武装，小尖峰直接触发（无需再等 2 小时）
    auto store2 = std::make_unique<GroupContextStore>(dbPath);
    EngagementService second(bot, store2.get(), nullptr, [&now] { return now; });
    second.setMemberProvider([](std::uint64_t) { return 100L; });
    std::vector<std::uint64_t> submitted;
    second.setSubmitTurn([&](std::uint64_t g) { submitted.push_back(g); });

    now += 30;
    for (int i = 0; i < 14; ++i)
    {
        now += 8;
        auto rec = record(group, 42, "小白", "聊爆了" + std::to_string(i), now);
        store2->append(rec);
        second.onGroupMessage(rec, false);
    }
    now += 61;
    second.pump(now);
    ASSERT_FALSE(submitted.empty()) << "重启后武装期不应重置";
    EXPECT_EQ(submitted.front(), group);
}

TEST(EngagementColdTest, DailyCapSurvivesRestart)
{
    TemporaryDirectory temporaryDirectory;
    const std::string dbPath = temporaryDirectory.path() + "/c.db";
    BotIdentity bot;
    bot.id = 987654321;
    bot.name = "Klein";
    const std::uint64_t group = 77502;

    std::int64_t now = 2'000'000;
    auto store1 = std::make_unique<GroupContextStore>(dbPath);
    EngagementService first(bot, store1.get(), nullptr, [&now] { return now; });
    first.setMemberProvider([](std::uint64_t) { return 100L; });

    auto feedBurst = [&](GroupContextStore &store, EngagementService &service, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            now += 8;
            auto rec = record(group, 42, "小白", "聊爆" + std::to_string(now + i), now);
            store.append(rec);
            service.onGroupMessage(rec, false);
        }
        now += 61;
        service.pump(now);
    };

    // 武装 + 学习
    for (int hour = 0; hour < 3; ++hour)
        for (int i = 0; i < 4; ++i)
        {
            now += 900;
            auto rec = record(group, 42, "小白", "日常" + std::to_string(hour * 4 + i), now);
            store1->append(rec);
            first.onGroupMessage(rec, false);
        }
    now += 120;

    // 消耗当日帽：3 次冷启动评估，每次后跨过 2h 冷却
    for (int round = 0; round < 3; ++round)
    {
        feedBurst(*store1, first, 12);
        now += 2 * 60 * 60;
    }
    store1.reset(); // "重启"

    // 重启后当日帽仍在（不再有 3 次机会）
    auto store2 = std::make_unique<GroupContextStore>(dbPath);
    EngagementService second(bot, store2.get(), nullptr, [&now] { return now; });
    second.setMemberProvider([](std::uint64_t) { return 100L; });
    std::vector<std::uint64_t> submitted;
    second.setSubmitTurn([&](std::uint64_t g) { submitted.push_back(g); });

    now += 61;
    second.pump(now); // 静默期检查
    feedBurst(*store2, second, 12);
    EXPECT_TRUE(submitted.empty()) << "当日帽应跨重启生效";
}
