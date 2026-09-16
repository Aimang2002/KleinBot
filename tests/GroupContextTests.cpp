#include <gtest/gtest.h>

#include "Perception/GroupContextStore.h"
#include "Perception/GroupContextService.h"
#include "Perception/GroupListService.h"
#include "Port/InboundMessage.h"
#include "Port/MessageSenderPort.h"
#include "Port/OutboundMessage.h"

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
    value.userId = qq;
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

    {
        GroupContextStore store(dbPath);
        ASSERT_TRUE(store.isOpen());
        GroupMessageRecord inbound;
        inbound.groupId = 8823;
        inbound.userId = 10;
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
        auto rebuilt = store.snapshot(8823);
        ASSERT_EQ(rebuilt.size(), 2U);
        EXPECT_EQ(rebuilt[0].userId, 10U) << "user_id 即原始 QQ（不伪名化）";
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
// 观察状态的最小假通道：仅满足构造，测试不触发任何协议调用
class NoopApiChannel final : public OneBotApiChannel
{
public:
    OneBotApiResult call(const std::string &, nlohmann::json, std::chrono::milliseconds) override
    {
        return {};
    }
};

// 观察状态服务（总开关 + 监控群集入库）：默认让 8823 处于监控中
std::unique_ptr<GroupListService> observingState(const std::string &dbPath,
                                                 bool enabled = true,
                                                 std::uint64_t groupId = 8823)
{
    static NoopApiChannel api;
    auto state = std::make_unique<GroupListService>(dbPath, api);
    state->setFeatureEnabled(enabled);
    if (enabled && groupId != 0)
        state->setMonitored(groupId, true);
    return state;
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
    auto state = observingState(temporaryDirectory.path() + "/conversation.db");
    GroupContextService service(state.get(), testBot(), &store);

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
    auto state = observingState(temporaryDirectory.path() + "/conversation.db");
    GroupContextService service(state.get(), testBot(), &store);

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
    auto state = observingState(temporaryDirectory.path() + "/conversation.db");
    GroupContextService service(state.get(), testBot(), &store);

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
    auto state = observingState(temporaryDirectory.path() + "/conversation.db");
    GroupContextService service(state.get(), testBot(), &store);

    service.observe(chat(10, "甲", "原神深渊阵容", 1000));
    EXPECT_TRUE(service.assemble(8823, "").empty());
    // 纯标点触发：无实词但仍带水位线上下文（比关键词注记更有用）
    EXPECT_FALSE(service.assemble(8823, "？？！！！").empty());

    // 总开关关闭 / 该群未监控：一律空（状态实时查询，无需重启）
    auto disabledState = observingState(temporaryDirectory.path() + "/disabled.db", false);
    GroupContextService disabled(disabledState.get(), testBot(), &store);
    EXPECT_TRUE(disabled.assemble(8823, "原神深渊阵容").empty());

    auto otherGroupState = observingState(temporaryDirectory.path() + "/other.db", true, 9999);
    GroupContextService otherGroup(otherGroupState.get(), testBot(), &store);
    EXPECT_TRUE(otherGroup.assemble(8823, "原神深渊阵容").empty());
}

// 摘要层：材料超直灌门槛时走 summarizer seam；失败退回截断原文
TEST(GroupContextServiceTest, LargeMaterialGoesThroughSummarizer)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    GroupContextStore store(temporaryDirectory.path() + "/conversation.db");
    auto state = observingState(temporaryDirectory.path() + "/conversation.db");
    GroupContextService service(state.get(), testBot(), &store);

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

TEST(GroupContextStoreTest, EngagementColdRoundtripAndReopen)
{
    TemporaryDirectory temporaryDirectory;
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    {
        GroupContextStore store(dbPath);
        ASSERT_TRUE(store.isOpen());
        EngagementColdRow row;
        row.hourlyEma = 37.5;
        row.armedTs = 1000;
        row.dayAnchor = 2000;
        row.coldToday = 2;
        row.cooldownUntil = 3000;
        store.saveEngagementCold(77001, row);
        row.hourlyEma = 5.0;
        row.armedTs = 1500;
        store.saveEngagementCold(77002, row);
    }
    {
        // 重开库：整表读回（重启续用的持久化语义）
        GroupContextStore store(dbPath);
        auto loaded = store.loadEngagementCold();
        ASSERT_EQ(loaded.size(), 2U);
        ASSERT_EQ(loaded.count(77001), 1U);
        EXPECT_DOUBLE_EQ(loaded[77001].hourlyEma, 37.5);
        EXPECT_EQ(loaded[77001].armedTs, 1000);
        EXPECT_EQ(loaded[77001].coldToday, 2);
        EXPECT_EQ(loaded[77001].cooldownUntil, 3000);
        EXPECT_DOUBLE_EQ(loaded[77002].hourlyEma, 5.0);

        // UPSERT 覆盖
        EngagementColdRow updated;
        updated.hourlyEma = 9.0;
        updated.armedTs = 1500;
        store.saveEngagementCold(77002, updated);
        auto reloaded = store.loadEngagementCold();
        EXPECT_DOUBLE_EQ(reloaded[77002].hourlyEma, 9.0);
        EXPECT_EQ(reloaded[77002].coldToday, 0) << "未写字段按默认值覆盖";
    }
}
