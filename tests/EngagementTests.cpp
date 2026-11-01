#include <gtest/gtest.h>

#include "Affect/BaselinePolicy.h"
#include "Affect/DecisionRecord.h"
#include "Affect/DecisionRecorder.h"
#include "Affect/DecisionStore.h"
#include "Affect/JudgePolicy.h"
#include "Perception/EngagementService.h"
#include "Perception/GroupContextService.h"
#include "Perception/GroupListService.h"
#include "Network/OneBotApiChannel.h"
#include "Port/OutboundDelivery.h"
#include "Action/WebToolGuidance.h"
#include "Tool/ImageToolGuidance.h"
#include "Tool/Tool.h"
#include "Tool/ToolRegistry.h"

#include <algorithm>
#include <chrono>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <sqlite3.h>

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
    // texts 只收文本（既有断言按它数"她说了几句"）；deliveries 按序收全部投递
    // （含工具出站的图片），非文本不再 std::get 抛异常
    void deliver(OutboundDelivery delivery) override
    {
        if (const auto *text = std::get_if<TextMessage>(&delivery.message))
            texts.push_back(text->content);
        deliveries.push_back(std::move(delivery));
    }
    std::vector<std::string> texts;
    std::vector<OutboundDelivery> deliveries;
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
    std::vector<ChatResponse> turnResponses; // 依次弹出；空则默认 pass
    std::vector<std::string> agentContracts; // 主 Agent 每次请求的 system 注记
    std::vector<std::vector<ChatMessage>> agentHistories;
    std::vector<std::vector<std::string>> agentSchemas; // 主 Agent 每次请求的工具表
    std::vector<std::string> workerSystems; // 杂务模型调用记录
    std::vector<std::string> workerUsers;

    explicit Harness(BotIdentity bot = {987654321, 123456789, "Klein"})
        : store(std::make_unique<GroupContextStore>(directory.path() + "/conversation.db")),
          sender(std::make_unique<FakeSender>()),
          service(std::make_unique<EngagementService>(
              bot, store.get(), sender.get(), [this] { return now; }))
    {
        service->setSubmitTurn([this](std::uint64_t group) { submittedTurns.push_back(group); });
        // 与组合根一致：她的出站经 recorder 入群缓冲并算会话活动（GroupContextService::
        // recordOutbound 的最小等价物；集成测试走真实实现）
        service->setOutboundRecorder(
            [this, bot](std::uint64_t group, const OutboundMessage &message,
                        const std::string &correlationId)
            {
                GroupMessageRecord self;
                self.groupId = group;
                self.userId = bot.id;
                self.nickname = bot.name;
                const auto *text = std::get_if<TextMessage>(&message);
                self.text = text != nullptr ? text->content : std::string("[图片]");
                self.timestamp = now;
                self.isSelf = true;
                self.correlationId = correlationId;
                store->append(self);
                service->onSelfActivity(group, now);
            });
        service->setMainAgent(
            [this](const std::string &contract, const std::vector<ChatMessage> &history,
                   const std::vector<std::string> &schemas)
            {
                agentContracts.push_back(contract);
                agentHistories.push_back(history);
                agentSchemas.push_back(schemas);
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
    for (int i = 0; i < 5; ++i)
        harness.feed(8823, "消息" + std::to_string(i), harness.now + i);
    harness.now += 90; // 跨过 75s 轮次节流 + 4s lull（最后消息在 now+4）
    harness.service->pump(harness.now);
    ASSERT_FALSE(harness.submittedTurns.empty());
    EXPECT_EQ(harness.submittedTurns.back(), 8823U);
}

TEST(EngagementSessionTest, LullTurnsHourlyCapStopsChattyFollowUps)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    // 小时内连续 8 个 lull 轮都放行
    for (int round = 0; round < 8; ++round)
    {
        for (int i = 0; i < 5; ++i)
            harness.feed(8823, "消息" + std::to_string(round) + "-" + std::to_string(i), harness.now);
        harness.now += 90;
        harness.service->pump(harness.now);
    }
    EXPECT_EQ(harness.submittedTurns.size(), 8U);
    // 第 9 次：消息量够、节流已过，但撞每小时帽不再触发（回得太勤是最大观感杀手）
    for (int i = 0; i < 5; ++i)
        harness.feed(8823, "再来", harness.now);
    harness.now += 90;
    harness.service->pump(harness.now);
    EXPECT_EQ(harness.submittedTurns.size(), 8U) << "每小时帽拦截第 9 个 lull 轮";
}

// 追问窗口不接 @ 她的消息（@ 路径会回答，否则双回）；她经 @ 路径开口后同样打开窗口
TEST(EngagementSessionTest, FollowUpWindowSkipsAtMessagesAndOpensAfterAtReply)
{
    Harness harness;
    harness.service->onAtActivated(8823, "在吗");
    harness.service->onSelfActivity(8823, harness.now); // 她经 @ 路径回了一句（问了个问题）
    harness.submittedTurns.clear();

    // 对方不引用不点名地回答 → 追问窗口给她一轮
    harness.now += 10;
    harness.feed(8823, "崩三前瞻主持人", harness.now);
    harness.now += 5;
    harness.service->pump(harness.now);
    ASSERT_EQ(harness.submittedTurns.size(), 1U);

    // 接着有人 @ 她：归 @ 路径，追问窗口不再起轮
    harness.now += 5;
    harness.feed(8823, "@Klein 懂了吗", harness.now, true, true);
    harness.now += 5;
    harness.service->pump(harness.now);
    EXPECT_EQ(harness.submittedTurns.size(), 1U) << "@ 消息不触发追问轮，防双回";

    // 之后一条普通消息又可以触发（仍在窗口与每次开口 2 轮的预算内）
    harness.now += 5;
    harness.feed(8823, "就那个lulu", harness.now);
    harness.now += 5;
    harness.service->pump(harness.now);
    EXPECT_EQ(harness.submittedTurns.size(), 2U);
}

TEST(EngagementSessionTest, FollowUpWindowTriggersTurnAfterHerSpeech)
{
    // 2026-10-30 21:31 现场的反面：她问"lulu是谁？"，群友不引用、不点名地答一句——
    // 此前要攒满 5 条 + 75s 节流才轮得到她。追问窗口里 1 条新消息 + 4s 静默就给她一轮
    //（资源预算；接不接仍由她在轮内决定）
    Harness harness;
    harness.service->onAtActivated(8823, "崩三前瞻");
    harness.feed(8823, "主持人lulu", harness.now);
    harness.turnResponses.push_back(sayResponse("lulu是谁？"));
    harness.service->runTurn(8823);
    ASSERT_EQ(harness.sender->texts.size(), 1U);
    EXPECT_EQ(harness.service->sessionOf(8823)->lastSpeakTs, harness.now);
    EXPECT_EQ(harness.service->sessionOf(8823)->followUpTurnsUsed, 0);
    ASSERT_TRUE(harness.submittedTurns.empty());

    // 群友答了一句（没引用、没提名字）：还没静默不提交，静默 4s 后一个追问轮
    harness.now += 3;
    harness.feed(8823, "崩三前瞻主持人", harness.now);
    harness.now += 2;
    harness.service->pump(harness.now);
    EXPECT_TRUE(harness.submittedTurns.empty()) << "还在说，没到静默";
    harness.now += 3;
    harness.service->pump(harness.now);
    ASSERT_EQ(harness.submittedTurns.size(), 1U) << "1 条新消息就够（lull 路径要 5 条 + 75s）";
    EXPECT_EQ(harness.submittedTurns.back(), 8823U);
    EXPECT_EQ(harness.service->sessionOf(8823)->followUpTurnsUsed, 1);
    harness.service->pump(harness.now + 3);
    EXPECT_EQ(harness.submittedTurns.size(), 1U) << "同一批消息不重复提交";
    harness.service->runTurn(8823); // 默认 pass：她选择先不说
    EXPECT_EQ(harness.service->sessionOf(8823)->consecutivePasses, 1) << "沉默的追问轮照常计 pass";

    // 又一条：第二个追问轮
    harness.now += 1;
    harness.feed(8823, "就是那个小尬剧的主持", harness.now);
    harness.now += 5;
    harness.service->pump(harness.now);
    ASSERT_EQ(harness.submittedTurns.size(), 2U);
    EXPECT_EQ(harness.service->sessionOf(8823)->followUpTurnsUsed, 2);
    harness.service->runTurn(8823); // pass

    // 第三条：本次开口的追问预算（2 轮）用完
    harness.now += 1;
    harness.feed(8823, "她去崩铁了", harness.now);
    harness.now += 5;
    harness.service->pump(harness.now);
    EXPECT_EQ(harness.submittedTurns.size(), 2U) << "每次开口至多 2 个追问轮";

    // 她再开口（被点名后回应）：追问预算归零
    harness.now += 1;
    harness.feed(8823, "Klein 懂了没", harness.now, true);
    ASSERT_EQ(harness.submittedTurns.size(), 3U) << "点名路径照常立即提交";
    harness.turnResponses.push_back(sayResponse("你是说崩三前瞻主持人去崩铁了？"));
    harness.service->runTurn(8823);
    EXPECT_EQ(harness.service->sessionOf(8823)->followUpTurnsUsed, 0);
    EXPECT_EQ(harness.service->sessionOf(8823)->lastSpeakTs, harness.now);
    const std::int64_t spokeAt = harness.now;

    harness.now += 2;
    harness.feed(8823, "对", harness.now);
    harness.now += 5;
    harness.service->pump(harness.now);
    ASSERT_EQ(harness.submittedTurns.size(), 4U) << "再开口后追问窗口重开";
    harness.service->runTurn(8823); // pass

    // 窗口过期（开口后 >120s）：1 条新消息不再触发
    harness.now = spokeAt + 116;
    harness.feed(8823, "lulu人挺好的", harness.now);
    harness.now = spokeAt + 121;
    harness.service->pump(harness.now);
    EXPECT_EQ(harness.submittedTurns.size(), 4U) << "窗口外回到 lull 路径（要 5 条）";
    EXPECT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Active);
}

TEST(EngagementSessionTest, FollowUpDoesNotConsumeLullHourlyCap)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);
    harness.turnResponses.push_back(sayResponse("这是啥？"));
    harness.service->runTurn(8823);
    const std::int64_t spokeAt = harness.now;

    // 窗口内同时满足 lull（5 条、节流已过、静默）与追问：只提交一轮（追问），
    // 不进 lull 每小时帽
    for (int i = 0; i < 5; ++i)
        harness.feed(8823, "群友" + std::to_string(i), spokeAt + 80 + i);
    harness.now = spokeAt + 90;
    harness.service->pump(harness.now);
    ASSERT_EQ(harness.submittedTurns.size(), 1U) << "追问已提交则本拍跳过 lull";
    auto session = harness.service->sessionOf(8823);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->followUpTurnsUsed, 1);
    EXPECT_TRUE(session->recentTurnTs.empty()) << "追问轮不计 lull 每小时帽";
    harness.service->runTurn(8823); // pass

    // 窗口外：lull 照常工作并记入每小时帽
    for (int i = 0; i < 5; ++i)
        harness.feed(8823, "后来" + std::to_string(i), spokeAt + 200 + i);
    harness.now = spokeAt + 210;
    harness.service->pump(harness.now);
    ASSERT_EQ(harness.submittedTurns.size(), 2U);
    session = harness.service->sessionOf(8823);
    EXPECT_EQ(session->recentTurnTs.size(), 1U);
    EXPECT_EQ(session->followUpTurnsUsed, 1) << "lull 轮不动追问预算";
}

TEST(EngagementContextTest, MoodLineUpdatesEveryThreeSpeaksAndFeedsMaterial)
{
    Harness harness;
    harness.service->setWorker([](const std::string &, const std::string &)
                               { return "有点困但来劲\n"; });
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "群友开场", harness.now); // 快照非空，runTurn 才会装配材料
    for (int round = 1; round <= 4; ++round)
    {
        harness.turnResponses.push_back(sayResponse("第" + std::to_string(round) + "句话"));
        harness.service->runTurn(8823);
    }
    auto session = harness.service->sessionOf(8823);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->moodLine, "有点困但来劲") << "第 3 轮后更新，去首尾空白";
    // 第 4 轮材料置顶注入情绪基调（跨轮惯性）
    ASSERT_FALSE(harness.agentHistories.empty());
    EXPECT_NE(harness.agentHistories.back().front().content.find("有点困但来劲"),
              std::string::npos);
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

TEST(EngagementSessionTest, AtMentionWhileActiveIsActivityNotAddress)
{
    // @ 的回答由 Message 的既有路径给出；会话只记活动，不再自己也回一句（2026-10-28 修复双回复）
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    const std::size_t before = harness.submittedTurns.size();
    harness.feed(8823, "@Klein 你觉得呢", harness.now + 5, true, true);
    EXPECT_EQ(harness.submittedTurns.size(), before);
    auto session = harness.service->sessionOf(8823);
    ASSERT_TRUE(session.has_value());
    EXPECT_FALSE(session->addressPending);
    EXPECT_EQ(session->lastActivityTs, harness.now + 5) << "@ 仍算会话活动";
    EXPECT_EQ(session->newMessagesSinceTurn, 1);
}

TEST(EngagementSessionTest, NameActivationFuseLimitsRate)
{
    Harness harness;
    const std::int64_t firstActivation = harness.now;
    // 最小间隔：首个提名字开会话；她离场后 60s 内再提名字被激活预算拦下
    harness.feed(8823, "Klein 在吗", harness.now, true);
    ASSERT_EQ(harness.submittedTurns.size(), 1U);
    harness.turnResponses.push_back(leaveResponse("只是顺口提到"));
    harness.service->runTurn(8823);
    ASSERT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Ended);

    harness.now += 10;
    harness.feed(8823, "Klein 呢", harness.now, true);
    EXPECT_EQ(harness.submittedTurns.size(), 1U) << "60s 内被最小间隔拦下";
    EXPECT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Ended)
        << "被拦下不开新会话";

    // 每小时上限：间隔 61s 反复提名字、每次她都离场，小时内至多 6 次激活
    for (int round = 1; round < 6; ++round)
    {
        harness.now += 61;
        harness.feed(8823, "Klein 第" + std::to_string(round) + "次", harness.now, true);
        ASSERT_EQ(harness.submittedTurns.size(), static_cast<std::size_t>(round + 1));
        ASSERT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Active);
        harness.turnResponses.push_back(leaveResponse("没在叫我"));
        harness.service->runTurn(8823);
        ASSERT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Ended);
    }
    harness.now += 61;
    harness.feed(8823, "Klein 第七次", harness.now, true);
    EXPECT_EQ(harness.submittedTurns.size(), 6U) << "小时内第 7 次被每小时上限拦下";
    EXPECT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Ended);

    // 滑动窗口滑过首次激活：恢复放行
    harness.now = firstActivation + 60 * 60;
    harness.feed(8823, "Klein 回来了吗", harness.now, true);
    EXPECT_EQ(harness.submittedTurns.size(), 7U);
    EXPECT_EQ(harness.service->sessionOf(8823)->state, EngagementState::Active);
}

TEST(EngagementSessionTest, NameMentionActivatesSessionAndSubmitsEntryTurn)
{
    // 2026-10-31 真机：别名点名"克莱茵在吗"经判定器误读被吞。现在提名字直接开会话
    // 进入场轮，由她本人读现场判断，不经杂务模型
    Harness harness;
    harness.feed(8823, "原神深渊怎么打", harness.now);
    harness.feed(8823, "克莱茵在吗", harness.now + 1, true);

    auto session = harness.service->sessionOf(8823);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->state, EngagementState::Active);
    EXPECT_TRUE(session->entryPending) << "入场轮待执行";
    EXPECT_EQ(session->topicLine, "克莱茵在吗");
    ASSERT_EQ(harness.submittedTurns.size(), 1U);
    EXPECT_EQ(harness.submittedTurns.front(), 8823U);
    EXPECT_TRUE(harness.workerSystems.empty()) << "激活不调杂务模型";

    // 入场轮：请求里告诉她提到她的是哪一句；她选了说话
    harness.turnResponses.push_back(sayResponse("在的，怎么了"));
    harness.service->runTurn(8823);
    ASSERT_EQ(harness.agentHistories.size(), 1U);
    const std::string &ask = harness.agentHistories.front().back().content;
    EXPECT_NE(ask.find("提到你的那句是：「克莱茵在吗」"), std::string::npos);
    ASSERT_EQ(harness.sender->texts.size(), 1U);
    EXPECT_EQ(harness.sender->texts.front(), "在的，怎么了");
    EXPECT_FALSE(harness.service->sessionOf(8823)->entryPending) << "入场轮执行后标记清除";
    EXPECT_TRUE(harness.workerSystems.empty());
}

TEST(EngagementSessionTest, ActiveSessionMergesMentionsInsteadOfReactivation)
{
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    const std::int64_t startBefore = harness.service->sessionOf(8823)->startTs;
    const std::size_t turnsBefore = harness.submittedTurns.size();
    // 会话存活期间提名字：并入会话（点名触发轮次），不重新激活、不出入场轮
    harness.now += 5;
    harness.feed(8823, "Klein 你怎么看", harness.now, true);
    EXPECT_EQ(harness.submittedTurns.size(), turnsBefore + 1);
    auto session = harness.service->sessionOf(8823);
    ASSERT_TRUE(session.has_value());
    EXPECT_TRUE(session->addressPending);
    EXPECT_FALSE(session->entryPending);
    EXPECT_EQ(session->startTs, startBefore);
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

TEST(EngagementContextTest, ContractPutsComprehensionFirst)
{
    // 2026-10-31：契约语用层改写为"先看懂再开口"，旧的"像真人一样说话"表演清单删除
    Harness harness;
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);
    harness.service->runTurn(8823);
    ASSERT_EQ(harness.agentContracts.size(), 1U);
    const std::string &contract = harness.agentContracts.front();

    EXPECT_NE(contract.find("调用 group_say"), std::string::npos) << "动作协议保留";
    EXPECT_NE(contract.find("leave_topic"), std::string::npos);
    const std::size_t comprehension = contract.find("先看懂再开口");
    ASSERT_NE(comprehension, std::string::npos);
    EXPECT_GT(comprehension, contract.find("绝不直接用文字回复")) << "排在动作协议之后";
    EXPECT_NE(contract.find("lulu是谁"), std::string::npos);
    EXPECT_NE(contract.find("不懂装懂"), std::string::npos);
    EXPECT_NE(contract.find("人格决定你说话的味道，不决定你说什么"), std::string::npos);
    EXPECT_NE(contract.find("名字被提到不等于在叫你"), std::string::npos);

    EXPECT_EQ(contract.find("不当老师"), std::string::npos);
    EXPECT_EQ(contract.find("反驳是例外"), std::string::npos);
    EXPECT_EQ(contract.find("像真人一样说话"), std::string::npos);
    EXPECT_EQ(contract.find("情绪有惯性"), std::string::npos);
    EXPECT_EQ(contract.find("klein_web_search"), std::string::npos) << "没开放搜索时不提搜索工具";
}

// ===== 会话轮注册表工具（inspect_image）=====

namespace
{
class FakeInspectTool : public Tool
{
public:
    std::string name() const override { return "inspect_image"; }
    std::string description() const override { return "查看历史图片的实际内容。"; }
    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"asset_id":{"type":"string"},"question":{"type":"string"}},"required":["question"]})";
    }
    // 使用规则由工具自带（ADR-022）：替身给出与 InspectImageTool 相同的规则
    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &) const override
    {
        return imageToolsUsageGuidance(scene);
    }
    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        receivedArgs.push_back(args);
        receivedContexts.push_back(ctx);
        return ToolResult{"图片视觉分析结果：一只猫", {}, {}};
    }

    std::vector<std::string> receivedArgs;
    std::vector<ToolContext> receivedContexts;
};

class FakeAdminTool : public Tool
{
public:
    std::string name() const override { return "admin_tool"; }
    std::string description() const override { return "管理员专用。"; }
    std::string parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    bool requiresAdmin() const override { return true; }
    ToolResult execute(const std::string &, const ToolContext &) override
    {
        executions += 1;
        return ToolResult{"管理员操作已执行", {}, {}};
    }

    int executions = 0;
};

// 生图工具替身：结果形态与 GenerateImageTool 成功分支一致（出站图 + 占位符 + 终态）
class FakeDrawTool : public Tool
{
public:
    std::string name() const override { return "generate_image"; }
    std::string description() const override { return "根据提示词生成图片。"; }
    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"prompt":{"type":"string"}},"required":["prompt"]})";
    }
    // 使用规则由工具自带（ADR-022）：替身给出与 GenerateImageTool 相同的规则
    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &) const override
    {
        return imageToolsUsageGuidance(scene);
    }
    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        receivedArgs.push_back(args);
        receivedContexts.push_back(ctx);
        return ToolResult{"图片已生成，可以继续询问它的内容，或要求重新发送。",
                          {ImageMessage{ImageMessage::Source::LocalPath, "/tmp/x.png"}},
                          "[image asset_id=gimg_t source=generated]",
                          true,
                          false,
                          true};
    }

    std::vector<std::string> receivedArgs;
    std::vector<ToolContext> receivedContexts;
};

ChatResponse inspectResponse(const std::string &id, const std::string &arguments)
{
    ChatResponse response;
    response.code = 200;
    response.finish_reason = "tool_calls";
    response.tool_calls.push_back({id, "inspect_image", arguments});
    return response;
}

bool schemasMention(const std::vector<std::string> &schemas, const std::string &name)
{
    for (const std::string &schema : schemas)
    {
        if (schema.find("\"" + name + "\"") != std::string::npos)
            return true;
    }
    return false;
}
} // namespace

TEST(EngagementToolTest, TurnOffersInspectImageOnlyWhenToolsSet)
{
    ToolRegistry registry;
    registry.registerTool(std::make_unique<FakeInspectTool>());

    // 未 setTools：工具表与契约保持原样
    Harness plain;
    plain.service->onAtActivated(8823, "话题");
    plain.feed(8823, "现场消息", plain.now);
    plain.service->runTurn(8823);
    ASSERT_EQ(plain.agentSchemas.size(), 1U);
    EXPECT_EQ(plain.agentSchemas.front().size(), 3U) << "group_say/leave_topic/recall_context";
    EXPECT_FALSE(schemasMention(plain.agentSchemas.front(), "inspect_image"));
    EXPECT_EQ(plain.agentContracts.front().find("瞥了一眼"), std::string::npos);

    // setTools：会话工具原样在前，inspect_image 追加在后，契约追加看图注
    Harness withTools;
    withTools.service->setTools(&registry, {"inspect_image"});
    withTools.service->onAtActivated(8823, "话题");
    withTools.feed(8823, "现场消息", withTools.now);
    withTools.service->runTurn(8823);
    ASSERT_EQ(withTools.agentSchemas.size(), 1U);
    const std::vector<std::string> &schemas = withTools.agentSchemas.front();
    ASSERT_EQ(schemas.size(), 4U);
    for (std::size_t index = 0; index < 3; ++index)
        EXPECT_EQ(schemas[index], plain.agentSchemas.front()[index]);
    EXPECT_EQ(schemas[3], *registry.schemaFor("inspect_image"));
    const std::string &contract = withTools.agentContracts.front();
    EXPECT_EQ(contract.rfind(plain.agentContracts.front(), 0), 0U) << "原契约逐字节保留为前缀";
    EXPECT_NE(contract.find("瞥了一眼"), std::string::npos);

    EXPECT_FALSE(registry.schemaFor("not_registered").has_value());
}

TEST(EngagementToolTest, TurnExecutesInspectImageThenSpeaks)
{
    ToolRegistry registry;
    auto inspect = std::make_unique<FakeInspectTool>();
    FakeInspectTool *inspectTool = inspect.get();
    registry.registerTool(std::move(inspect));

    Harness harness;
    harness.service->setTools(&registry, {"inspect_image"});
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "[image asset_id=gimg_x source=inbound | 一张图]", harness.now);
    harness.turnResponses.push_back(
        inspectResponse("ii-1", R"({"asset_id":"gimg_x","question":"什么图"})"));
    harness.turnResponses.push_back(sayResponse("是只猫"));
    harness.service->runTurn(8823);

    // 工具拿到的是群作用域、无个人语境
    ASSERT_EQ(inspectTool->receivedContexts.size(), 1U);
    EXPECT_EQ(inspectTool->receivedContexts.front().shared_scope_id, 8823U);
    EXPECT_EQ(inspectTool->receivedContexts.front().user_id, 0U);
    const nlohmann::json args = nlohmann::json::parse(inspectTool->receivedArgs.front());
    EXPECT_EQ(args.value("asset_id", ""), "gimg_x");
    EXPECT_EQ(args.value("question", ""), "什么图");

    // 第二次请求带回 assistant(tool_calls) + tool 结果
    ASSERT_EQ(harness.agentHistories.size(), 2U);
    const std::vector<ChatMessage> &second = harness.agentHistories[1];
    ASSERT_EQ(second.size(), 3U);
    EXPECT_EQ(second[1].role, "assistant");
    ASSERT_EQ(second[1].tool_calls.size(), 1U);
    EXPECT_EQ(second[1].tool_calls.front().id, "ii-1");
    EXPECT_EQ(second[1].tool_calls.front().name, "inspect_image");
    EXPECT_EQ(nlohmann::json::parse(second[1].tool_calls.front().arguments).value("asset_id", ""),
              "gimg_x");
    EXPECT_EQ(second[2].role, "tool");
    EXPECT_EQ(second[2].tool_call_id, "ii-1");
    EXPECT_EQ(second[2].content, "图片视觉分析结果：一只猫");

    ASSERT_EQ(harness.sender->texts.size(), 1U);
    EXPECT_EQ(harness.sender->texts.front(), "是只猫");
    EXPECT_EQ(harness.service->sessionOf(8823)->turnCount, 1);
}

TEST(EngagementToolTest, AdminToolsNeverOfferedToTurns)
{
    ToolRegistry registry;
    auto inspect = std::make_unique<FakeInspectTool>();
    FakeInspectTool *inspectTool = inspect.get();
    registry.registerTool(std::move(inspect));
    auto admin = std::make_unique<FakeAdminTool>();
    FakeAdminTool *adminTool = admin.get();
    registry.registerTool(std::move(admin));

    Harness harness;
    harness.service->setTools(&registry, {"inspect_image", "admin_tool", "not_registered"});
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);

    // 模型硬调管理员工具（与 inspect_image 同一回复）：只回灌"未知工具"，不执行
    ChatResponse both = inspectResponse("ii-1", R"({"question":"看看"})");
    both.tool_calls.push_back({"ad-1", "admin_tool", "{}"});
    harness.turnResponses.push_back(both);
    harness.turnResponses.push_back(passResponse());
    harness.service->runTurn(8823);

    ASSERT_FALSE(harness.agentSchemas.empty());
    const std::vector<std::string> &schemas = harness.agentSchemas.front();
    EXPECT_TRUE(schemasMention(schemas, "inspect_image"));
    EXPECT_FALSE(schemasMention(schemas, "admin_tool"));
    EXPECT_FALSE(schemasMention(schemas, "not_registered"));
    EXPECT_EQ(schemas.size(), 4U);

    EXPECT_EQ(adminTool->executions, 0);
    EXPECT_EQ(inspectTool->receivedArgs.size(), 1U);
    ASSERT_EQ(harness.agentHistories.size(), 2U);
    const std::vector<ChatMessage> &second = harness.agentHistories[1];
    ASSERT_EQ(second.size(), 4U);
    EXPECT_EQ(second[3].tool_call_id, "ad-1");
    EXPECT_EQ(second[3].content.rfind("错误：未知工具", 0), 0U);
}

TEST(EngagementToolTest, RecallAndInspectInOneResponseBothRunBeforeNextRequest)
{
    ToolRegistry registry;
    auto inspect = std::make_unique<FakeInspectTool>();
    FakeInspectTool *inspectTool = inspect.get();
    registry.registerTool(std::move(inspect));

    Harness harness;
    harness.service->setTools(&registry, {"inspect_image"});
    harness.service->onAtActivated(8823, "话题");
    // 32 条：窗口只装最后 30 条，最早 2 条只能靠召回
    for (int index = 0; index < 32; ++index)
    {
        const std::string text = index < 2 ? "船新版本介绍" + std::to_string(index)
                                           : "闲聊" + std::to_string(index);
        harness.feedRaw(8823, text, harness.now + index);
    }

    ChatResponse combined;
    combined.code = 200;
    combined.finish_reason = "tool_calls";
    combined.tool_calls.push_back({"rc-1", "recall_context", R"({"keywords":["船新","版本"]})"});
    combined.tool_calls.push_back({"ii-1", "inspect_image", R"({"question":"什么图"})"});
    harness.turnResponses.push_back(combined);
    harness.turnResponses.push_back(sayResponse("原来如此"));
    harness.service->runTurn(8823);

    // 两个调用都在下一次请求之前执行完毕：一条 assistant + 两条 tool
    ASSERT_EQ(harness.agentHistories.size(), 2U);
    const std::vector<ChatMessage> &second = harness.agentHistories[1];
    ASSERT_EQ(second.size(), 4U);
    EXPECT_EQ(second[1].role, "assistant");
    EXPECT_EQ(second[1].tool_calls.size(), 2U);
    EXPECT_EQ(second[2].role, "tool");
    EXPECT_EQ(second[2].tool_call_id, "rc-1");
    EXPECT_NE(second[2].content.find("船新版本介绍0"), std::string::npos);
    EXPECT_EQ(second[3].role, "tool");
    EXPECT_EQ(second[3].tool_call_id, "ii-1");
    EXPECT_EQ(second[3].content, "图片视觉分析结果：一只猫");
    EXPECT_EQ(inspectTool->receivedArgs.size(), 1U);
    EXPECT_EQ(harness.service->sessionOf(8823)->recallUsed, 1);
    ASSERT_EQ(harness.sender->texts.size(), 1U);
    EXPECT_EQ(harness.sender->texts.front(), "原来如此");
}

TEST(EngagementToolTest, ToolRoundCapEndsAsPass)
{
    ToolRegistry registry;
    auto inspect = std::make_unique<FakeInspectTool>();
    FakeInspectTool *inspectTool = inspect.get();
    registry.registerTool(std::move(inspect));

    Harness harness;
    harness.service->setTools(&registry, {"inspect_image"});
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);
    for (int index = 0; index < 4; ++index)
        harness.turnResponses.push_back(
            inspectResponse("ii-" + std::to_string(index), R"({"question":"再看看"})"));
    harness.service->runTurn(8823);

    // 3 趟工具轮后模型仍要工具：本拍按沉默处理
    EXPECT_EQ(inspectTool->receivedArgs.size(), 3U);
    EXPECT_EQ(harness.agentHistories.size(), 4U);
    EXPECT_TRUE(harness.sender->texts.empty());
    auto session = harness.service->sessionOf(8823);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->state, EngagementState::Active);
    EXPECT_EQ(session->consecutivePasses, 1);
    EXPECT_EQ(session->turnCount, 0);
}

TEST(EngagementToolTest, TurnDeliversToolOutboundImageAndRecordsPlaceholder)
{
    ToolRegistry registry;
    auto draw = std::make_unique<FakeDrawTool>();
    FakeDrawTool *drawTool = draw.get();
    registry.registerTool(std::move(draw));

    Harness harness;
    harness.service->setTools(&registry, {"generate_image"});
    struct Recorded
    {
        std::uint64_t groupId;
        OutboundMessage message;
        std::string correlationId;
    };
    std::vector<Recorded> recorded;
    harness.service->setOutboundRecorder(
        [&recorded](std::uint64_t groupId, const OutboundMessage &message,
                    const std::string &correlationId)
        { recorded.push_back({groupId, message, correlationId}); });
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "能画一只猫吗", harness.now);

    ChatResponse drawCall;
    drawCall.code = 200;
    drawCall.finish_reason = "tool_calls";
    drawCall.tool_calls.push_back({"gi-1", "generate_image", R"({"prompt":"一只猫"})"});
    harness.turnResponses.push_back(drawCall);
    harness.turnResponses.push_back(sayResponse("画好了"));
    harness.service->runTurn(8823);

    // 生图在群作用域执行（存群库），无个人语境
    ASSERT_EQ(drawTool->receivedContexts.size(), 1U);
    EXPECT_EQ(drawTool->receivedContexts.front().shared_scope_id, 8823U);
    EXPECT_EQ(drawTool->receivedContexts.front().user_id, 0U);

    // 先投递工具出站图，再投递她的那句话
    ASSERT_EQ(harness.sender->deliveries.size(), 2U);
    const OutboundDelivery &imageDelivery = harness.sender->deliveries[0];
    const auto *image = std::get_if<ImageMessage>(&imageDelivery.message);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->source, ImageMessage::Source::LocalPath);
    EXPECT_EQ(image->data, "/tmp/x.png");
    const auto *target = std::get_if<GroupMessageTarget>(&imageDelivery.target);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->group_id, "8823");
    EXPECT_FALSE(imageDelivery.correlation_id.empty());

    // 入库 seam 拿到同一张图与同一个关联键（占位符由 GroupContextService 生成），
    // 随后她的那句话也经同一个 seam 入库（会话轮唯一出站路径）
    ASSERT_EQ(recorded.size(), 2U);
    EXPECT_EQ(recorded.front().groupId, 8823U);
    const auto *recordedImage = std::get_if<ImageMessage>(&recorded.front().message);
    ASSERT_NE(recordedImage, nullptr);
    EXPECT_EQ(recordedImage->data, "/tmp/x.png");
    EXPECT_EQ(recorded.front().correlationId, imageDelivery.correlation_id);

    // 工具结果照常回灌，模型随后定案说话
    ASSERT_EQ(harness.agentHistories.size(), 2U);
    const std::vector<ChatMessage> &second = harness.agentHistories[1];
    ASSERT_EQ(second.size(), 3U);
    EXPECT_EQ(second[2].role, "tool");
    EXPECT_EQ(second[2].tool_call_id, "gi-1");
    EXPECT_EQ(second[2].content, "图片已生成，可以继续询问它的内容，或要求重新发送。");

    EXPECT_TRUE(std::holds_alternative<TextMessage>(harness.sender->deliveries[1].message));
    const auto *recordedText = std::get_if<TextMessage>(&recorded[1].message);
    ASSERT_NE(recordedText, nullptr);
    EXPECT_EQ(recordedText->content, "画好了");
    EXPECT_EQ(recorded[1].correlationId, harness.sender->deliveries[1].correlation_id);
    ASSERT_EQ(harness.sender->texts.size(), 1U);
    EXPECT_EQ(harness.sender->texts.front(), "画好了");
    EXPECT_EQ(harness.service->sessionOf(8823)->turnCount, 1);
}

TEST(EngagementToolTest, ImageToolNoteMentionsGenerateWhenOffered)
{
    ToolRegistry registry;
    registry.registerTool(std::make_unique<FakeDrawTool>());

    // 只开放 generate_image（没有 inspect_image）也追加图片注
    Harness harness;
    harness.service->setTools(&registry, {"generate_image"});
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);
    harness.service->runTurn(8823);

    ASSERT_EQ(harness.agentContracts.size(), 1U);
    const std::string &contract = harness.agentContracts.front();
    EXPECT_NE(contract.find("瞥了一眼"), std::string::npos);
    EXPECT_NE(contract.find("generate_image"), std::string::npos);
    EXPECT_TRUE(schemasMention(harness.agentSchemas.front(), "generate_image"));
}

namespace
{
// 联网搜索替身：名字与 KleinWebSearchToolName 一致
class FakeWebSearchTool : public Tool
{
public:
    std::string name() const override { return "klein_web_search"; }
    std::string description() const override { return "联网搜索。"; }
    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"query":{"type":"string"}},"required":["query"]})";
    }
    // 使用规则由工具自带（ADR-022）：替身给出与 WebSearchAction 相同的规则
    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &context) const override
    {
        return webSearchUsageGuidance(scene, context);
    }
    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        receivedArgs.push_back(args);
        receivedContexts.push_back(ctx);
        return ToolResult{"搜索结果：lulu 是崩坏3前瞻直播的主持人", {}, {}};
    }

    std::vector<std::string> receivedArgs;
    std::vector<ToolContext> receivedContexts;
};
} // namespace

TEST(EngagementToolTest, WebSearchOfferedAndNoteAppendedWhenRegistered)
{
    // 2026-10-31：看不懂的人名/梗可以在会话轮里查（未配置联网时 setTools 丢弃，不提）
    ToolRegistry registry;
    auto search = std::make_unique<FakeWebSearchTool>();
    FakeWebSearchTool *searchTool = search.get();
    registry.registerTool(std::move(search));

    Harness harness;
    harness.service->setTools(&registry, {"klein_web_search", "klein_web_fetch"});
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "主持人lulu", harness.now);
    ChatResponse searchCall;
    searchCall.code = 200;
    searchCall.finish_reason = "tool_calls";
    searchCall.tool_calls.push_back({"ws-1", "klein_web_search", R"({"query":"lulu 崩坏3 主持人"})"});
    harness.turnResponses.push_back(searchCall);
    harness.turnResponses.push_back(sayResponse("哦是前瞻那个主持啊"));
    harness.service->runTurn(8823);

    ASSERT_FALSE(harness.agentSchemas.empty());
    EXPECT_TRUE(schemasMention(harness.agentSchemas.front(), "klein_web_search"));
    EXPECT_FALSE(schemasMention(harness.agentSchemas.front(), "klein_web_fetch")) << "未注册的丢弃";
    const std::string &contract = harness.agentContracts.front();
    EXPECT_NE(contract.find("不可信外部资料"), std::string::npos);
    EXPECT_NE(contract.find("klein_web_search"), std::string::npos);
    EXPECT_EQ(contract.find("klein_web_fetch"), std::string::npos) << "只提实际开放的工具";
    EXPECT_EQ(contract.find("瞥了一眼"), std::string::npos) << "没开放图片工具不追加看图注";

    // 搜索结果走既有工具轮回灌（群作用域、无个人语境），随后定案说话
    ASSERT_EQ(searchTool->receivedContexts.size(), 1U);
    EXPECT_EQ(searchTool->receivedContexts.front().shared_scope_id, 8823U);
    EXPECT_EQ(searchTool->receivedContexts.front().user_id, 0U);
    ASSERT_EQ(harness.agentHistories.size(), 2U);
    ASSERT_EQ(harness.agentHistories[1].size(), 3U);
    EXPECT_EQ(harness.agentHistories[1][2].tool_call_id, "ws-1");
    EXPECT_EQ(harness.agentHistories[1][2].content, "搜索结果：lulu 是崩坏3前瞻直播的主持人");
    ASSERT_EQ(harness.sender->texts.size(), 1U);
    EXPECT_EQ(harness.sender->texts.front(), "哦是前瞻那个主持啊");

    // 未注册联网工具：不给 schema、不追加注
    ToolRegistry empty;
    Harness plain;
    plain.service->setTools(&empty, {"klein_web_search", "klein_web_fetch"});
    plain.service->onAtActivated(8823, "话题");
    plain.feed(8823, "主持人lulu", plain.now);
    plain.service->runTurn(8823);
    ASSERT_EQ(plain.agentSchemas.size(), 1U);
    EXPECT_FALSE(schemasMention(plain.agentSchemas.front(), "klein_web_search"));
    EXPECT_EQ(plain.agentContracts.front().find("不可信外部资料"), std::string::npos);
    EXPECT_EQ(plain.agentContracts.front().find("klein_web_search"), std::string::npos);
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

TEST(EngagementIntegrationTest, ObserveFeedsEngagementAndNameMentionActivates)
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
    int workerCalls = 0;
    engagement.setWorker([&workerCalls](const std::string &, const std::string &)
                         {
                             ++workerCalls;
                             return std::string();
                         });
    std::vector<std::uint64_t> turns;
    engagement.setSubmitTurn([&](std::uint64_t group) { turns.push_back(group); });

    // 群里聊出语境，有人提名字无 @：observe 喂 onGroupMessage → 直接开会话 +
    // 入场轮提交（叫没叫她由她本人在入场轮判断，不经杂务模型）
    contextService.observe(inboundChat(8823, 10, "原神深渊十二层怎么打", 1000));
    contextService.observe(inboundChat(8823, 20, "深渊阵容带钟离稳一点", 1001));
    EXPECT_FALSE(engagement.sessionOf(8823).has_value()) << "没提名字不开会话";
    contextService.observe(inboundChat(8823, 30, "Klein 你深渊用的什么阵容", 1002));
    ASSERT_EQ(turns.size(), 1U);
    ASSERT_TRUE(engagement.sessionOf(8823).has_value());
    EXPECT_EQ(engagement.sessionOf(8823)->state, EngagementState::Active);
    EXPECT_TRUE(engagement.sessionOf(8823)->entryPending);
    EXPECT_EQ(engagement.sessionOf(8823)->topicLine, "Klein 你深渊用的什么阵容");
    EXPECT_EQ(workerCalls, 0);

    // 会话存活期间再提名字：并入会话（点名触发轮次），不重新激活
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
    EXPECT_NE(harness.agentHistories.back().front().content.find("看不懂他们在聊什么"),
              std::string::npos)
        << "冷入场 ask：先读懂，看不懂就问/查/不出声（2026-10-31）";
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

// M1 影子记录：主动接话与沉默都产出一决策一执行，动作不变。
TEST(EngagementDecisionRecordTest, RecordsSpeakAndPassWithSharedContract)
{
    Harness harness;
    affect::DecisionStore store(harness.directory.path() + "/affect.db");
    ASSERT_TRUE(store.isOpen());
    affect::DecisionRecorder recorder(&store, [&harness] { return harness.now; });
    harness.service->setDecisionRecorder(&recorder);

    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);
    harness.turnResponses.push_back(sayResponse("接一句"));
    harness.service->runTurn(8823);

    auto decisions = store.recentDecisions(10);
    ASSERT_EQ(decisions.size(), 1U);
    EXPECT_EQ(decisions.front().source, "engagement_turn");
    EXPECT_EQ(decisions.front().plan.action, "group_say");
    EXPECT_EQ(decisions.front().createdAt, harness.now);
    EXPECT_EQ(store.executionCount(decisions.front().decisionId), 1U);

    harness.now += 60;
    harness.feed(8823, "又来一条", harness.now);
    harness.turnResponses.push_back(passResponse());
    harness.service->runTurn(8823);

    decisions = store.recentDecisions(10);
    ASSERT_EQ(decisions.size(), 2U);
    bool foundSilence = false;
    for (const auto &decision : decisions)
    {
        if (decision.plan.action == "no_reply")
        {
            foundSilence = true;
            EXPECT_EQ(store.executionCount(decision.decisionId), 1U);
        }
    }
    EXPECT_TRUE(foundSilence);
}

// 提名字入场轮由她本人决定：沉默/离场都不发言，并留下 engagement_turn 决策记录
//（日后据此看"她选择了沉默"）。入场轮沉默沿用 Entry 语义：计一拍 pass，会话不收场
TEST(EngagementDecisionRecordTest, NameMentionEntryPassStaysQuietAndRecordsNoReply)
{
    Harness harness;
    affect::DecisionStore store(harness.directory.path() + "/affect.db");
    ASSERT_TRUE(store.isOpen());
    affect::DecisionRecorder recorder(&store, [&harness] { return harness.now; });
    harness.service->setDecisionRecorder(&recorder);

    // 只是在聊她，不是叫她：入场轮选沉默
    harness.feed(8823, "昨天克莱茵那个梗笑死我了", harness.now, true);
    ASSERT_EQ(harness.submittedTurns.size(), 1U);
    harness.turnResponses.push_back(passResponse());
    harness.service->runTurn(8823);

    EXPECT_TRUE(harness.sender->texts.empty()) << "她选择沉默，不发言";
    auto session = harness.service->sessionOf(8823);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->state, EngagementState::Ended) << "入场轮沉默 = 不加入，直接收场";
    EXPECT_FALSE(session->entryPending);
    auto decisions = store.recentDecisions(10);
    ASSERT_EQ(decisions.size(), 1U);
    EXPECT_EQ(decisions.front().source, "engagement_turn");
    EXPECT_EQ(decisions.front().plan.action, "no_reply");
    EXPECT_EQ(store.executionCount(decisions.front().decisionId), 1U);

    // 另一个群：入场轮选 leave_topic（无最后一句）→ 不发言、会话结束
    harness.now += 5;
    harness.feed(8824, "克莱茵这个名字挺好听", harness.now, true);
    ASSERT_EQ(harness.submittedTurns.size(), 2U);
    harness.turnResponses.push_back(leaveResponse("只是在聊我，不是叫我"));
    harness.service->runTurn(8824);

    EXPECT_TRUE(harness.sender->texts.empty());
    EXPECT_EQ(harness.service->sessionOf(8824)->state, EngagementState::Ended);
    decisions = store.recentDecisions(10);
    ASSERT_EQ(decisions.size(), 2U);
    bool foundLeave = false;
    for (const auto &decision : decisions)
    {
        if (decision.plan.action == "leave_topic")
        {
            foundLeave = true;
            EXPECT_EQ(decision.source, "engagement_turn");
        }
    }
    EXPECT_TRUE(foundLeave);
}

// ---- R1.b 决策点影子接线 ----
namespace
{
// 执行记录读回（status + deviation_reason）：DecisionStore 没有按决策列执行的读接口
std::vector<std::pair<std::string, std::string>> executionsOf(const std::string &dbPath,
                                                              const std::string &decisionId)
{
    std::vector<std::pair<std::string, std::string>> rows;
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        sqlite3_close(db);
        return rows;
    }
    sqlite3_stmt *statement = nullptr;
    sqlite3_prepare_v2(db,
                       "SELECT status, deviation_reason FROM affect_executions"
                       " WHERE decision_id=?1 ORDER BY created_at, execution_id;",
                       -1, &statement, nullptr);
    sqlite3_bind_text(statement, 1, decisionId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW)
        rows.emplace_back(reinterpret_cast<const char *>(sqlite3_column_text(statement, 0)),
                          reinterpret_cast<const char *>(sqlite3_column_text(statement, 1)));
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return rows;
}

// 一行决策 + 它的执行，去掉随机 ID 后序列化，用于整行对拍
std::string decisionRowWithoutIds(const affect::DecisionRecord &decision, const std::string &dbPath)
{
    std::string row = std::to_string(decision.createdAt) + "|" + decision.source + "|" +
                      std::to_string(decision.groupId) + "|" + std::to_string(decision.userId) +
                      "|" + decision.triggerMessageId + "|" + decision.plan.toJson().dump() + "|" +
                      decision.policyVersion + "|" + decision.planVersion + "|" +
                      decision.stateJson + "|" + decision.fallbackReason;
    for (const auto &[status, deviation] : executionsOf(dbPath, decision.decisionId))
        row += "|exec:" + status + "/" + deviation;
    return row;
}

std::string flattenHistory(const std::vector<ChatMessage> &history)
{
    std::string flat;
    for (const ChatMessage &message : history)
    {
        flat += message.role + ":" + message.content + ":" + message.tool_call_id;
        for (const ToolCallRequest &call : message.tool_calls)
            flat += "[" + call.id + "," + call.name + "," + call.arguments + "]";
        flat += "\n";
    }
    return flat;
}

// 一次固定剧本（两拍 lull：先说一句、再沉默）在给定策略下的全部可观测输出
struct TurnTranscript
{
    std::vector<std::string> contracts;
    std::vector<std::string> histories;
    std::vector<std::vector<std::string>> schemas;
    std::vector<std::string> texts;
    std::vector<std::string> decisionRows; // 时间升序
    std::vector<affect::DecisionRecord> decisions;
    std::vector<std::vector<std::pair<std::string, std::string>>> executions;
};

const char *const kMaterialMarker = "原文标记甲乙丙";

TurnTranscript runTwoLullTurns(affect::DecisionPolicy *policy, bool setPolicy = true,
                               bool enforce = false)
{
    Harness harness;
    const std::string dbPath = harness.directory.path() + "/affect.db";
    affect::DecisionStore store(dbPath);
    affect::DecisionRecorder recorder(&store, [&harness] { return harness.now; });
    harness.service->setDecisionRecorder(&recorder);
    if (setPolicy)
    {
        harness.service->setPolicy(policy);
        harness.service->setEnforce(enforce);
    }

    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, std::string("现场消息") + kMaterialMarker, harness.now);
    harness.turnResponses.push_back(sayResponse("接一句"));
    harness.service->runTurn(8823);
    harness.now += 60;
    harness.feed(8823, "又来一条", harness.now);
    harness.turnResponses.push_back(passResponse());
    harness.service->runTurn(8823);

    TurnTranscript transcript;
    transcript.contracts = harness.agentContracts;
    for (const auto &history : harness.agentHistories)
        transcript.histories.push_back(flattenHistory(history));
    transcript.schemas = harness.agentSchemas;
    transcript.texts = harness.sender->texts;
    transcript.decisions = store.recentDecisions(10);
    std::reverse(transcript.decisions.begin(), transcript.decisions.end());
    for (const auto &decision : transcript.decisions)
    {
        transcript.decisionRows.push_back(decisionRowWithoutIds(decision, dbPath));
        transcript.executions.push_back(executionsOf(dbPath, decision.decisionId));
    }
    return transcript;
}

void expectSameRequestsAndActions(const TurnTranscript &actual, const TurnTranscript &legacy)
{
    EXPECT_EQ(actual.contracts, legacy.contracts);
    EXPECT_EQ(actual.histories, legacy.histories);
    EXPECT_EQ(actual.schemas, legacy.schemas);
    EXPECT_EQ(actual.texts, legacy.texts);
}

std::string judgeReply(const std::string &action)
{
    return R"({"schema_version":1,"action":")" + action +
           R"(","tool_policy":")" + (action == "no_reply" ? "none" : "inherit") + R"("})";
}
} // namespace

// 影子期：judge 对 lull 建议 no_reply，但她照常被请求、照常说话；记录里是 judge 的计划、
// 执行标 shadow；请求体与无策略时逐字相同；state_json 是派生状态、不含材料原文
TEST(EngagementDecisionPointTest, ShadowJudgeRecordsPlanButKeepsBaselineAction)
{
    const TurnTranscript legacy = runTwoLullTurns(nullptr, false);

    std::vector<std::string> judgePrompts;
    affect::JudgePolicy judge(
        [&judgePrompts](const std::string &, const std::string &user)
        {
            judgePrompts.push_back(user);
            return judgeReply("no_reply");
        },
        "judge-model");
    const TurnTranscript shadow = runTwoLullTurns(&judge);

    expectSameRequestsAndActions(shadow, legacy);
    ASSERT_EQ(shadow.texts.size(), 1U) << "计划说 no_reply，影子期她仍照常说了";
    ASSERT_EQ(judgePrompts.size(), 2U);
    EXPECT_NE(judgePrompts.front().find(kMaterialMarker), std::string::npos)
        << "原文只在内存里给 judge";

    ASSERT_EQ(shadow.decisions.size(), 2U);
    for (std::size_t index = 0; index < shadow.decisions.size(); ++index)
    {
        const affect::DecisionRecord &decision = shadow.decisions[index];
        EXPECT_EQ(decision.source, "engagement_turn");
        EXPECT_EQ(decision.plan.action, "no_reply");
        EXPECT_EQ(decision.policyVersion, "judge_v1");
        EXPECT_EQ(decision.planVersion, "judge_plan_v1");
        EXPECT_TRUE(decision.fallbackReason.empty());
        const nlohmann::json state = nlohmann::json::parse(decision.stateJson);
        EXPECT_EQ(state["trigger_kind"], "lull");
        EXPECT_TRUE(state.contains("turn_count"));
        EXPECT_EQ(state["judge_model"], "judge-model");
        EXPECT_TRUE(state.contains("judge_latency_ms"));
        EXPECT_EQ(decision.stateJson.find(kMaterialMarker), std::string::npos)
            << "state_json 不得含群聊原文";
        ASSERT_EQ(shadow.executions[index].size(), 1U);
        EXPECT_EQ(shadow.executions[index].front().second, "shadow");
    }
    EXPECT_EQ(shadow.executions[0].front().first, "queued");
    EXPECT_EQ(shadow.executions[1].front().first, "skipped");
    EXPECT_EQ(nlohmann::json::parse(shadow.decisions[1].stateJson)["turn_count"], 1)
        << "会话统计取自锁内快照";
}

// 基线策略：不装配 card、不调 decide，记录与未设策略时整行一致（含 state_json="{}"）
TEST(EngagementDecisionPointTest, BaselinePolicyRecordsIdenticalToLegacy)
{
    const TurnTranscript legacy = runTwoLullTurns(nullptr, false);
    affect::BaselinePolicy baseline;
    const TurnTranscript withBaseline = runTwoLullTurns(&baseline);

    expectSameRequestsAndActions(withBaseline, legacy);
    ASSERT_EQ(legacy.decisionRows.size(), 2U);
    EXPECT_EQ(withBaseline.decisionRows, legacy.decisionRows);
    EXPECT_EQ(legacy.decisions[0].stateJson, "{}");
    EXPECT_EQ(legacy.decisions[0].plan.action, "group_say");
    EXPECT_EQ(legacy.decisions[1].plan.action, "no_reply");
    EXPECT_EQ(legacy.executions[0].front().second, "");
}

// 空策略（含 enforce=true）等价基线
TEST(EngagementDecisionPointTest, NullPolicyBehavesAsBaseline)
{
    const TurnTranscript legacy = runTwoLullTurns(nullptr, false);
    const TurnTranscript nullPolicy = runTwoLullTurns(nullptr, true, true);
    expectSameRequestsAndActions(nullPolicy, legacy);
    EXPECT_EQ(nullPolicy.decisionRows, legacy.decisionRows);
}

// 追问窗口轮：提问文本与工具表与改前一致，card 标 follow_up_window
TEST(EngagementDecisionPointTest, FollowUpWindowTurnKeepsRequestBody)
{
    const auto runScenario = [](affect::DecisionPolicy *policy, std::string *triggerKind)
    {
        Harness harness;
        const std::string dbPath = harness.directory.path() + "/affect.db";
        affect::DecisionStore store(dbPath);
        affect::DecisionRecorder recorder(&store, [&harness] { return harness.now; });
        harness.service->setDecisionRecorder(&recorder);
        harness.service->setPolicy(policy);

        harness.service->onAtActivated(8823, "崩三前瞻");
        harness.feed(8823, "主持人lulu", harness.now);
        harness.turnResponses.push_back(sayResponse("lulu是谁？"));
        harness.service->runTurn(8823);
        harness.now += 3;
        harness.feed(8823, "崩三前瞻主持人", harness.now);
        harness.now += 5;
        harness.service->pump(harness.now);
        EXPECT_EQ(harness.submittedTurns.size(), 1U);
        EXPECT_TRUE(harness.service->sessionOf(8823)->followUpPending);
        harness.service->runTurn(8823);
        EXPECT_FALSE(harness.service->sessionOf(8823)->followUpPending);

        if (triggerKind != nullptr)
        {
            const auto decisions = store.recentDecisions(1);
            *triggerKind = decisions.empty()
                ? std::string()
                : nlohmann::json::parse(decisions.front().stateJson).value("trigger_kind", "");
        }
        std::vector<std::string> requests;
        for (std::size_t index = 0; index < harness.agentHistories.size(); ++index)
            requests.push_back(harness.agentContracts[index] + "\n" +
                               flattenHistory(harness.agentHistories[index]));
        return requests;
    };

    const std::vector<std::string> legacy = runScenario(nullptr, nullptr);
    affect::JudgePolicy judge([](const std::string &, const std::string &)
                              { return judgeReply("group_say"); },
                              "judge-model");
    std::string triggerKind;
    const std::vector<std::string> shadow = runScenario(&judge, &triggerKind);
    ASSERT_EQ(legacy.size(), 2U);
    EXPECT_EQ(shadow, legacy);
    EXPECT_EQ(triggerKind, "follow_up_window");
}

// 五种轮次各一：card 的 trigger_kind 与来源
TEST(EngagementDecisionPointTest, CardTriggerKindPerTurn)
{
    Harness harness;
    const std::string dbPath = harness.directory.path() + "/affect.db";
    affect::DecisionStore store(dbPath);
    affect::DecisionRecorder recorder(&store, [&harness] { return harness.now; });
    harness.service->setDecisionRecorder(&recorder);
    affect::JudgePolicy judge(
        [](const std::string &, const std::string &) { return judgeReply("group_say"); },
        "judge-model");
    harness.service->setPolicy(&judge);
    const auto latestTrigger = [&store]()
    {
        const auto decisions = store.recentDecisions(1);
        const nlohmann::json state = nlohmann::json::parse(decisions.front().stateJson);
        return decisions.front().source + "/" + state.value("trigger_kind", "");
    };

    // lull：会话存活、无待执行标志
    harness.service->onAtActivated(8823, "话题");
    harness.feed(8823, "现场消息", harness.now);
    harness.service->runTurn(8823);
    EXPECT_EQ(latestTrigger(), "engagement_turn/lull");

    // address：会话存活时被点名
    harness.now += 5;
    harness.feed(8823, "Klein 你觉得呢", harness.now, true);
    harness.turnResponses.push_back(sayResponse("我觉得可以"));
    harness.service->runTurn(8823);
    EXPECT_EQ(latestTrigger(), "engagement_turn/address");

    // follow_up_window：她刚开口，群友接了一句并静默
    harness.now += 3;
    harness.feed(8823, "哈哈", harness.now);
    harness.now += 5;
    harness.service->pump(harness.now);
    harness.service->runTurn(8823);
    EXPECT_EQ(latestTrigger(), "engagement_turn/follow_up_window");

    // name_mention：另一个群无会话时提到她的名字
    harness.now += 5;
    harness.feed(8824, "克莱茵这个名字挺好听", harness.now, true);
    harness.service->runTurn(8824);
    EXPECT_EQ(latestTrigger(), "engagement_turn/name_mention");

    // cold_spike：冷启动入场
    ColdFlow flow(harness);
    flow.setMembers(200);
    flow.warm(3, 4);
    flow.spike(12);
    EXPECT_EQ(latestTrigger(), "engagement_cold_entry/cold_spike");
}
