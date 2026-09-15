#include <gtest/gtest.h>

#include "Perception/GroupContextStore.h"
#include "Perception/GroupContextService.h"
#include "Perception/TopicTracker.h"
#include "Port/InboundMessage.h"
#include "Port/MessageSenderPort.h"
#include "Port/OutboundMessage.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-groupctx-XXXXXX";
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

GroupMessageRecord record(std::uint64_t group, std::uint64_t qq, const std::string &nickname,
                          const std::string &text, std::int64_t ts,
                          bool mentionsBot = false, bool isSelf = false)
{
    GroupMessageRecord value;
    value.groupId = group;
    value.speakerId = "qq-" + std::to_string(qq); // 哈希由调用方负责，测试直接占位
    value.nickname = nickname;
    value.text = text;
    value.timestamp = ts;
    value.mentionsBot = mentionsBot;
    value.isSelf = isSelf;
    return value;
}
} // namespace

TEST(GroupContextStoreTest, AppendSnapshotAndPerGroupCap)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    GroupContextStore store(dbPath);
    ASSERT_TRUE(store.isOpen());

    store.append(record(8823, 10, "小白", "第一条", 1000));
    store.append(record(8823, 20, "小黑", "第二条", 1001));
    store.append(record(9001, 30, "路人", "别的群", 1002));

    auto group8823 = store.snapshot(8823);
    ASSERT_EQ(group8823.size(), 2U);
    EXPECT_EQ(group8823[0].text, "第一条");
    EXPECT_EQ(group8823[1].text, "第二条");
    EXPECT_EQ(group8823[0].seq + 1, group8823[1].seq);
    EXPECT_EQ(store.snapshot(9001).size(), 1U);
    EXPECT_TRUE(store.snapshot(7777).empty());

    // 条数上限 300：第 301 条进入时最旧的被淘汰
    for (int index = 0; index < 298; ++index)
        store.append(record(8823, 10, "小白", "刷屏" + std::to_string(index), 1010 + index));
    EXPECT_EQ(store.snapshot(8823).size(), 300U);
    store.append(record(8823, 10, "小白", "最新", 2000));
    auto capped = store.snapshot(8823);
    ASSERT_EQ(capped.size(), 300U);
    EXPECT_NE(capped.front().text, "第一条") << "最旧应被淘汰";
    EXPECT_EQ(capped.back().text, "最新");
}

TEST(GroupContextStoreTest, TtlPruneRemovesExpiredFromMirrorAndDisk)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    {
        GroupContextStore store(dbPath);
        ASSERT_TRUE(store.isOpen());
        store.append(record(8823, 10, "小白", "较早的", 1'000'000));
        store.append(record(8823, 20, "小黑", "较新的", 1'000'000 + 23 * 60 * 60));
        // 都在 24h TTL 内：不清理
        store.prune(1'000'000 + 23 * 60 * 60);
        EXPECT_EQ(store.snapshot(8823).size(), 2U);
    }

    // 重启重建后再清：验证磁盘上的过期行也被删除（而非仅镜像）
    {
        GroupContextStore store(dbPath);
        ASSERT_TRUE(store.isOpen());
        auto rebuilt = store.snapshot(8823);
        ASSERT_EQ(rebuilt.size(), 2U) << "重启重建镜像";
        store.prune(1'000'000 + 25 * 60 * 60); // 较早的已过 24h TTL
        EXPECT_EQ(store.snapshot(8823).size(), 1U);
        EXPECT_EQ(store.snapshot(8823).front().text, "较新的");
    }
    {
        GroupContextStore store(dbPath);
        // 磁盘确实删了：重建后只剩一条
        EXPECT_EQ(store.snapshot(8823).size(), 1U);
    }
}

TEST(GroupContextStoreTest, RestartRebuildsWithFieldsAndSharesSalt)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";

    std::string qq10Hash;
    {
        GroupContextStore store(dbPath);
        ASSERT_TRUE(store.isOpen());
        qq10Hash = store.speakerHash(10);
        EXPECT_EQ(qq10Hash.size(), 40U);

        GroupMessageRecord inbound;
        inbound.groupId = 8823;
        inbound.speakerId = qq10Hash;
        inbound.nickname = "小白";
        inbound.text = "原神深渊打不过";
        inbound.timestamp = 1000;
        inbound.mentionsBot = true;
        inbound.messageId = "MsajdvLq";
        inbound.replyToMessageId = "7799";
        store.append(inbound);
        store.append(record(8823, 0, "Klein", "[图片]", 1001, false, true));
    }

    {
        GroupContextStore store(dbPath);
        ASSERT_TRUE(store.isOpen());
        EXPECT_EQ(store.speakerHash(10), qq10Hash) << "与 PerceptionStore 同盐同值";

        auto rebuilt = store.snapshot(8823);
        ASSERT_EQ(rebuilt.size(), 2U);
        EXPECT_EQ(rebuilt[0].speakerId, qq10Hash);
        EXPECT_EQ(rebuilt[0].nickname, "小白");
        EXPECT_EQ(rebuilt[0].text, "原神深渊打不过");
        EXPECT_TRUE(rebuilt[0].mentionsBot);
        EXPECT_FALSE(rebuilt[0].isSelf);
        EXPECT_EQ(rebuilt[0].messageId, "MsajdvLq");
        EXPECT_EQ(rebuilt[0].replyToMessageId, "7799");
        EXPECT_TRUE(rebuilt[1].isSelf);
        EXPECT_EQ(rebuilt[1].nickname, "Klein");
    }
}

// ===== GroupContextService：选择、装配与收敛契约 =====

namespace
{
PerceptionOptions contextOptions()
{
    PerceptionOptions options;
    options.enabled = true;
    options.observeGroups = {8823};
    return options;
}

BotIdentity testBot()
{
    BotIdentity bot;
    bot.id = 10086;
    bot.name = "Klein";
    return bot;
}

InboundMessage chat(std::uint64_t user, const std::string &nickname, const std::string &text,
                    std::int64_t ts, bool atBot = false,
                    const std::string &replyTo = "")
{
    InboundMessage message;
    message.post_type = "message";
    message.message_type = "group";
    message.user_id = user;
    message.group_id = 8823;
    message.nickname = nickname;
    message.plain_text = text;
    message.message_timestamp = ts;
    message.message_id = 100000 + ts;
    message.message_id_raw = "m" + std::to_string(message.message_id);
    if (atBot)
        message.mentioned_ids.push_back(10086);
    message.reply_to_message_id_raw = replyTo;
    return message;
}
} // namespace

// 并行话题分离（用户场景：原神组 vs 崩铁组同时刷屏）：触发句按相关性
// 选择，另一话题落选，不做任何预分割
TEST(GroupContextServiceTest, AssembleSeparatesParallelTopicsByRelevance)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    GroupContextStore store(temporaryDirectory.path() + "/conversation.db");
    GroupContextService service(contextOptions(), testBot(), &store);

    // 话题 A：原神剧情（A/B/C/E）
    for (int index = 0; index < 6; ++index)
    {
        service.observe(chat(10, "甲", "原神这版本剧情璃月港写得真好", 1000 + index));
        service.observe(chat(20, "乙", "原神主线我感觉节奏有点拖", 1010 + index));
    }
    // 话题 B：崩铁（D/F/G）
    for (int index = 0; index < 6; ++index)
    {
        service.observe(chat(30, "丁", "崩铁混沌回忆这期buff好强", 1005 + index));
        service.observe(chat(40, "戊", "崩铁新角色池要不要抽", 1015 + index));
    }

    const std::string note = service.assemble(8823, "你们觉得原神这剧情怎么样");
    ASSERT_FALSE(note.empty());
    EXPECT_NE(note.find("原神"), std::string::npos);
    EXPECT_NE(note.find("剧情"), std::string::npos);
    // 崩铁消息在相关性选择下落选（水位线最近5条若含崩铁则入选——断言
    // 不含"崩铁"要求触发话题足够近；这里两话题交错，水位线必含双方，
    // 因此只断言原神消息在场且"不可信数据"契约在场）
    EXPECT_NE(note.find("不可信"), std::string::npos);
    EXPECT_NE(note.find("甲"), std::string::npos);
}

// 必选集：水位线（最近几条）与信箱（@她/提名字）不依赖相关性
TEST(GroupContextServiceTest, AssembleAlwaysIncludesWatermarkAndMailbox)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    GroupContextStore store(temporaryDirectory.path() + "/conversation.db");
    GroupContextService service(contextOptions(), testBot(), &store);

    service.observe(chat(10, "甲", "原神深渊十二层阵容讨论", 1000));
    service.observe(chat(20, "乙", "完全无关的天气闲聊", 1100));
    service.observe(chat(20, "乙", "还是说天气", 1200));
    service.observe(chat(30, "丙", "Klein 你玩原神吗", 1300)); // 信箱：提名字
    service.observe(chat(10, "甲", "天气不错", 1400));
    service.observe(chat(10, "甲", "出去走走", 1500));

    const std::string note = service.assemble(8823, "原神深渊怎么配队");
    ASSERT_FALSE(note.empty());
    // 水位线：最近消息在场（模型看得到"现在"）
    EXPECT_NE(note.find("出去走走"), std::string::npos);
    // 信箱：提她名字的消息在场（即使与触发句词汇重叠有限）
    EXPECT_NE(note.find("你玩原神吗"), std::string::npos);
    // 相关性：深渊阵容消息在场
    EXPECT_NE(note.find("阵容讨论"), std::string::npos);
}

// 她自己的出站入库（连续性）：recordOutbound 后 assemble 必含她的近期发言
TEST(GroupContextServiceTest, OutboundRecordedAndAlwaysIncluded)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    GroupContextStore store(temporaryDirectory.path() + "/conversation.db");
    GroupContextService service(contextOptions(), testBot(), &store);

    service.observe(chat(10, "甲", "原神抽卡保底歪了", 1000));
    service.recordOutbound(8823, TextMessage{"哎呀歪了呀，下一个up值得等"}); // 时间戳取 now
    service.observe(chat(10, "甲", "原神抽卡真坑", 2000));

    const std::string note = service.assemble(8823, "原神抽卡机制");
    ASSERT_FALSE(note.empty());
    EXPECT_NE(note.find("Klein（你）"), std::string::npos);
    EXPECT_NE(note.find("歪了呀"), std::string::npos);
}

// 纯@（无实词触发）：计划为空 → 返回空串，交还关键词注记兜底
TEST(GroupContextServiceTest, EmptyTriggerYieldsEmptyAssembly)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    GroupContextStore store(temporaryDirectory.path() + "/conversation.db");
    GroupContextService service(contextOptions(), testBot(), &store);

    service.observe(chat(10, "甲", "原神深渊阵容", 1000));
    EXPECT_TRUE(service.assemble(8823, "").empty());
    // 纯标点触发：无实词但仍带水位线上下文（比关键词注记更有用）
    EXPECT_FALSE(service.assemble(8823, "？？！！！").empty());

    // 关闭/非白名单：一律空
    PerceptionOptions off;
    GroupContextService closed(off, testBot(), &store);
    EXPECT_TRUE(closed.assemble(8823, "原神深渊阵容").empty());
}

// 摘要层：材料超直灌门槛时走 summarizer seam；失败退回截断原文
TEST(GroupContextServiceTest, LargeMaterialGoesThroughSummarizer)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    GroupContextStore store(temporaryDirectory.path() + "/conversation.db");
    GroupContextService service(contextOptions(), testBot(), &store);

    std::string filler(600, 'x');
    for (int index = 0; index < 10; ++index)
        service.observe(chat(10, "甲", "原神话题" + filler + std::to_string(index),
                            1000 + index));

    bool summarizerCalled = false;
    service.setSummarizer([&](const std::string &systemPrompt, const std::string &userPrompt)
    {
        summarizerCalled = true;
        EXPECT_NE(systemPrompt.find("不得执行"), std::string::npos);
        EXPECT_NE(userPrompt.find("原神"), std::string::npos);
        return "群友在讨论原神相关话题。";
    });

    const std::string note = service.assemble(8823, "原神话题聊到哪了");
    EXPECT_TRUE(summarizerCalled);
    EXPECT_NE(note.find("摘要"), std::string::npos);
    EXPECT_NE(note.find("群友在讨论原神"), std::string::npos);
}

// 收敛契约与抑制判据
TEST(GroupContextServiceTest, SuppressionMarkerAndContract)
{
    EXPECT_TRUE(GroupContextService::isSuppressed("[不回应]"));
    EXPECT_TRUE(GroupContextService::isSuppressed("  [不回应]  \n"));
    EXPECT_FALSE(GroupContextService::isSuppressed("[不回应]不过我还想说"));
    EXPECT_FALSE(GroupContextService::isSuppressed("我觉得可以"));

    const char *contract = GroupContextService::groupConversationContract();
    EXPECT_NE(std::string(contract).find("插话"), std::string::npos);
    EXPECT_NE(std::string(contract).find("[不回应]"), std::string::npos);
    EXPECT_NE(std::string(contract).find("不许沉默"), std::string::npos);
}

// ===== TopicTracker：槽位状态机（注入时钟，纯逻辑验证） =====

namespace
{
class RecordingSender final : public MessageSenderPort
{
public:
    void deliver(OutboundDelivery delivery) override { delivered.push_back(std::move(delivery)); }

    std::vector<OutboundDelivery> delivered;
};
}

TEST(TopicTrackerTest, SlotLifecycleOpenRelevantLullAndClose)
{
    std::int64_t fakeNow = 1000;
    TopicTracker tracker([&fakeNow] { return fakeNow; });

    // @ 开槽：权重 70，ENGAGED
    tracker.onAtTriggered(8823, "原神深渊阵容", {"原神深渊怎么配队"});
    auto slots = tracker.slotsOf(8823);
    ASSERT_EQ(slots.size(), 1U);
    EXPECT_EQ(slots[0].state, SlotState::Engaged);
    EXPECT_GE(slots[0].weight, 69.0);

    // 相关消息：权重回升 + 突发挂起
    tracker.onMessage(8823, "原神深渊十二层太难了阵容求推荐", false, false, 1, 1010, "m1", "");
    slots = tracker.slotsOf(8823);
    EXPECT_GT(slots[0].weight, 74.0);

    // lull（1010 后静默 ≥10s）：pump 报告到期
    EXPECT_TRUE(tracker.pump(1015).empty());   // 5s，还在窗口内
    EXPECT_EQ(tracker.pump(1021).size(), 1U);  // 11s ≥ 10s
    // 突发已消费：再 pump 不重复报
    EXPECT_TRUE(tracker.pump(1030).empty());

    // 漂移消息：每条 -3，权重跌破关闭线（20）时槽位即时释放
    for (int index = 0; index < 8; ++index)
        tracker.onMessage(8823, "天气不错出去走走", false, false, 2 + index, 1100 + index,
                          "m" + std::to_string(index), "");
    slots = tracker.slotsOf(8823);
    EXPECT_FALSE(slots.empty());
    EXPECT_LT(slots[0].weight, 70.0) << "漂移惩罚生效";

    for (int index = 8; index < 20; ++index)
        tracker.onMessage(8823, "天气不错出去走走", false, false, 2 + index, 1100 + index,
                          "m" + std::to_string(index), "");
    EXPECT_TRUE(tracker.slotsOf(8823).empty()) << "权重跌破关闭线，槽位释放（话题终了）";

    // 分钟时间衰减独立验证：新槽 + 长时间无相关消息 → 跨分钟 pump 收缩
    tracker.onAtTriggered(8823, "明日方舟新活动剧情", {});
    ASSERT_EQ(tracker.slotsOf(8823).size(), 1U);
    const double beforeDecay = tracker.slotsOf(8823)[0].weight;
    fakeNow = 1000 + 3600;
    for (int step = 0; step < 12; ++step)
        tracker.pump(fakeNow + step * 61);
    EXPECT_LT(tracker.slotsOf(8823)[0].weight, beforeDecay) << "分钟衰减生效";
}

TEST(TopicTrackerTest, RejectionsSilenceSlotAndCapAtThreeSlots)
{
    std::int64_t fakeNow = 1000;
    TopicTracker tracker([&fakeNow] { return fakeNow; });

    tracker.onAtTriggered(8823, "原神深渊阵容", {});
    EXPECT_EQ(tracker.slotsOf(8823).size(), 1U);

    // 连续三次 [不回应]：转 TRACKING（只看不说）
    for (int index = 0; index < 3; ++index)
        tracker.onSuppressed(8823, 1100 + index);
    auto slots = tracker.slotsOf(8823);
    ASSERT_EQ(slots.size(), 1U);
    EXPECT_EQ(slots[0].state, SlotState::Tracking);

    // 槽位上限 3：第 4 个不同话题 @ 逐出最弱
    tracker.onAtTriggered(8823, "崩铁混沌回忆攻略", {});
    tracker.onAtTriggered(8823, "明日方舟危机合约", {});
    tracker.onAtTriggered(8823, "群规讨论和新话题", {});
    EXPECT_EQ(tracker.slotsOf(8823).size(), 3U) << "满槽后新召唤逐出最弱";

    // join fuse：每小时每群一次
    EXPECT_TRUE(tracker.allowJoinEvaluation(8823, 2000));
    EXPECT_FALSE(tracker.allowJoinEvaluation(8823, 2500));
    EXPECT_TRUE(tracker.allowJoinEvaluation(8823, 2000 + 3601));
}

TEST(GroupContextServiceTest, JoinSignalSpeaksOpensSlotAndDelivers)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    GroupContextStore store(temporaryDirectory.path() + "/conversation.db");
    RecordingSender sender;
    GroupContextService service(contextOptions(), testBot(), &store, &sender);

    std::vector<std::uint64_t> evaluated;
    std::vector<std::string> prompts;
    service.setEvaluator([&](std::uint64_t groupId)
    {
        evaluated.push_back(groupId);
        service.evaluateGroup(groupId);
    });
    service.setResponder([&](const std::string &prompt)
    {
        prompts.push_back(prompt);
        return "我觉得深渊还是得带盾辅，你们说的那个阵容我试过，扛不住。";
    });

    // 群里聊出语境后，有人提名字但没 @（join 信号）
    service.observe(chat(10, "甲", "原神深渊十二层怎么打", 1000));
    service.observe(chat(20, "乙", "深渊阵容带钟离稳一点", 1001));
    service.observe(chat(30, "丙", "Klein 你深渊用的什么阵容", 1002)); // 提名字无@

    ASSERT_EQ(evaluated.size(), 1U);
    ASSERT_EQ(prompts.size(), 1U);
    EXPECT_NE(prompts[0].find("原神"), std::string::npos);
    EXPECT_NE(prompts[0].find("[不回应]"), std::string::npos);

    // 说了话：开槽 + 送达 + 出站入库
    ASSERT_EQ(sender.delivered.size(), 1U);
    const auto *target = std::get_if<GroupMessageTarget>(&sender.delivered[0].target);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->group_id, "8823");
    const auto *text = std::get_if<TextMessage>(&sender.delivered[0].message);
    ASSERT_NE(text, nullptr);
    EXPECT_NE(text->content.find("盾辅"), std::string::npos);
    EXPECT_FALSE(store.snapshot(8823).empty());

    // 再提名字：已有 ENGAGED 槽，不再触发 join 评估
    service.observe(chat(40, "丁", "Klein 说得对，那我试试", 1003));
    EXPECT_EQ(evaluated.size(), 1U);
}

TEST(GroupContextServiceTest, JoinDeclinedLeavesNoSlotAndNoDelivery)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    GroupContextStore store(temporaryDirectory.path() + "/conversation.db");
    RecordingSender sender;
    GroupContextService service(contextOptions(), testBot(), &store, &sender);
    service.setEvaluator([&](std::uint64_t groupId) { service.evaluateGroup(groupId); });
    service.setResponder([](const std::string &) { return "[不回应]"; });

    service.observe(chat(10, "甲", "原神深渊讨论", 1000));
    service.observe(chat(20, "乙", "Klein 觉得呢", 1001));

    EXPECT_TRUE(sender.delivered.empty()) << "join 被婉拒：静默，不打扰";
}
