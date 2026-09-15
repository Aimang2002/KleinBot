#include <gtest/gtest.h>

#include "Network/OneBotApiChannel.h"
#include "Perception/GroupListService.h"
#include "Perception/PerceptionChannel.h"
#include "Perception/PerceptionStore.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <fstream>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-perception-XXXXXX";
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

InboundMessage groupMessage(std::uint64_t user, std::uint64_t group,
                            const std::string &text, std::time_t timestamp)
{
    InboundMessage message;
    message.post_type = "message";
    message.message_type = "group";
    message.user_id = user;
    message.group_id = group;
    message.plain_text = text;
    message.message_timestamp = timestamp;
    return message;
}

InboundMessage privateMessage(std::uint64_t user, const std::string &text)
{
    InboundMessage message;
    message.post_type = "message";
    message.message_type = "private";
    message.user_id = user;
    message.plain_text = text;
    message.message_timestamp = 1000;
    return message;
}

// 观察状态的最小假通道：仅满足构造，测试不触发协议调用
class NoopApiChannel final : public OneBotApiChannel
{
public:
    OneBotApiResult call(const std::string &, nlohmann::json, std::chrono::milliseconds) override
    {
        return {};
    }
};

// 观察状态服务（数据库真值）：默认开启总开关并监控 8823
std::unique_ptr<GroupListService> observingState(const std::string &dbPath, bool enabled = true)
{
    static NoopApiChannel api;
    auto state = std::make_unique<GroupListService>(dbPath, api);
    state->setFeatureEnabled(enabled);
    if (enabled)
        state->setMonitored(8823, true);
    return state;
}

std::vector<std::string> readAffinityRows(const std::string &dbPath)
{
    std::vector<std::string> rows;
    sqlite3 *database = nullptr;
    if (sqlite3_open(dbPath.c_str(), &database) != SQLITE_OK)
    {
        sqlite3_close(database);
        return rows;
    }
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(database,
                           "SELECT speaker_id, group_id, observed_count, interaction_count,"
                           " last_seen_ts FROM affinity ORDER BY speaker_id;",
                           -1, &statement, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            rows.push_back(std::string(reinterpret_cast<const char *>(sqlite3_column_text(statement, 0))) + "," +
                           std::to_string(sqlite3_column_int64(statement, 1)) + "," +
                           std::to_string(sqlite3_column_int64(statement, 2)) + "," +
                           std::to_string(sqlite3_column_int64(statement, 3)) + "," +
                           std::to_string(sqlite3_column_int64(statement, 4)));
        }
        sqlite3_finalize(statement);
    }
    sqlite3_close(database);
    return rows;
}

std::string readFileBytes(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
        return {};
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}
} // namespace

TEST(PerceptionChannelTest, ObservesOnlyWhitelistedEnabledGroups)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    PerceptionStore store(dbPath);

    // 总开关关闭：一切 no-op
    auto disabledState = observingState(temporaryDirectory.path() + "/disabled.db", false);
    PerceptionChannel closed(disabledState.get(), &store);
    closed.observeMessage(groupMessage(10, 8823, "你好", 1000));
    closed.observeInteraction(groupMessage(10, 8823, "在吗", 1001));
    closed.flushDue(100000);
    EXPECT_TRUE(readAffinityRows(dbPath).empty());
    EXPECT_TRUE(closed.hotTopics(8823, 5).empty());

    // 开启但群不在白名单 / 私聊：不观察
    auto state = observingState(temporaryDirectory.path() + "/state.db");
    PerceptionChannel channel(state.get(), &store);
    channel.observeMessage(groupMessage(10, 9999, "别的群", 1000));
    channel.observeInteraction(privateMessage(10, "私聊不算群观察"));
    channel.flushDue(100000);
    EXPECT_TRUE(readAffinityRows(dbPath).empty());
    EXPECT_TRUE(channel.hotTopics(9999, 5).empty());
}

TEST(PerceptionChannelTest, AffinityAggregatesAndFlushesToStore)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    PerceptionStore store(dbPath);
    auto state = observingState(temporaryDirectory.path() + "/state.db");
    PerceptionChannel channel(state.get(), &store);

    channel.observeMessage(groupMessage(10, 8823, "早上好", 1000));
    channel.observeMessage(groupMessage(10, 8823, "吃什么", 1005));
    channel.observeInteraction(groupMessage(10, 8823, "@机器人 帮我查天气", 1010));
    channel.observeMessage(groupMessage(20, 8823, "路过", 1000));

    channel.flushDue(100000);
    const auto rows = readAffinityRows(dbPath);
    ASSERT_EQ(rows.size(), 2U);
    const std::string speaker10 = store.speakerHash(10);
    const std::string speaker20 = store.speakerHash(20);
    EXPECT_EQ(rows[0], (speaker20 < speaker10 ? speaker20 : speaker10) +
                          ",8823," + (speaker20 < speaker10 ? "1,0,1000" : "2,1,1010"));
    EXPECT_EQ(rows[1], (speaker20 < speaker10 ? speaker10 : speaker20) +
                          ",8823," + (speaker20 < speaker10 ? "2,1,1010" : "1,0,1000"));

    // 节流：间隔不足 30s 且未达 delta 阈值，新计数滞留内存不写库
    channel.observeMessage(groupMessage(10, 8823, "再来一条", 1015));
    channel.flushDue(100001);
    EXPECT_EQ(readAffinityRows(dbPath).size(), 2U);

    // 间隔到期：增量累加落库
    channel.flushDue(100031);
    const auto accumulated = readAffinityRows(dbPath);
    ASSERT_EQ(accumulated.size(), 2U);
    bool speaker10Updated = false;
    for (const auto &row : accumulated)
    {
        if (row.rfind(speaker10 + ",8823,", 0) == 0)
        {
            EXPECT_EQ(row, speaker10 + ",8823,3,1,1015");
            speaker10Updated = true;
        }
    }
    EXPECT_TRUE(speaker10Updated);
}

TEST(PerceptionChannelTest, WindowEvictsByCount)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    auto state = observingState(temporaryDirectory.path() + "/state.db");
    PerceptionChannel channel(state.get(), nullptr);

    // 600 条不同 ASCII 词消息：条数上限 500，最旧 100 条连同计数淘汰
    for (int index = 0; index < 600; ++index)
        channel.observeMessage(groupMessage(10, 8823, "gg" + std::to_string(index),
                                            1000 + index));

    const auto topics = channel.hotTopics(8823, 100000);
    ASSERT_EQ(topics.size(), 500U);
    EXPECT_EQ(topics[0].second, 1.0); // 每个词只出现一次，无热点梯度
    bool hasOldest = false;
    bool hasNewest = false;
    for (const auto &topic : topics)
    {
        if (topic.first == "gg0")
            hasOldest = true;
        if (topic.first == "gg599")
            hasNewest = true;
    }
    EXPECT_FALSE(hasOldest) << "最旧 100 条应已淘汰";
    EXPECT_TRUE(hasNewest);
}

TEST(PerceptionChannelTest, WindowEvictsByTime)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    auto state = observingState(temporaryDirectory.path() + "/state.db");
    PerceptionChannel channel(state.get(), nullptr);

    channel.observeMessage(groupMessage(10, 8823, "旧话题", 100000));
    // 2 小时窗口外的新消息触发对旧条目的淘汰
    channel.observeMessage(groupMessage(10, 8823, "新话题", 100000 + 7201));

    const auto topics = channel.hotTopics(8823, 200000);
    bool hasOld = false;
    bool hasNew = false;
    double sharedGram = 0.0;
    for (const auto &topic : topics)
    {
        if (topic.first == "旧话")
            hasOld = true;
        if (topic.first == "新话")
            hasNew = true;
        if (topic.first == "话题")
            sharedGram = topic.second;
    }
    EXPECT_FALSE(hasOld);
    EXPECT_TRUE(hasNew);
    EXPECT_EQ(sharedGram, 1.0) << "共享 2-gram 只剩新消息的贡献";
}

// D7 红线：消息原文永不落盘、不驻留内存——库文件字节级找不到原文，
// 内存里只存在 2-gram 碎片（不可能是完整句子）
TEST(PerceptionChannelTest, RedLinePlaintextNeverPersisted)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    PerceptionStore store(dbPath);
    auto state = observingState(temporaryDirectory.path() + "/state.db");
    PerceptionChannel channel(state.get(), &store);

    // 全小写标记：normalizeText 只小写化 ASCII，避免大小写转换让碎片对不上原文
    const std::string secret = "绝密原文标记zeroonetwo不要外传";
    channel.observeMessage(groupMessage(10, 8823, secret, 1000));
    channel.flushDue(100000);

    // 主库与 WAL 页都要干净
    EXPECT_EQ(readFileBytes(dbPath).find(secret), std::string::npos);
    const std::string wal = readFileBytes(dbPath + "-wal");
    EXPECT_EQ(wal.find(secret), std::string::npos);

    // 内存侧：hotTopics 只允许 2-gram / ASCII 词碎片
    for (const auto &topic : channel.hotTopics(8823, 100))
    {
        EXPECT_NE(topic.first, secret);
        EXPECT_NE(secret.find(topic.first), std::string::npos)
            << "内存中的 n-gram 必须来自原文的碎片，而不是额外内容";
    }
}

TEST(PerceptionChannelTest, HotTopicsReturnsTopNOrdered)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    auto state = observingState(temporaryDirectory.path() + "/state.db");
    PerceptionChannel channel(state.get(), nullptr);

    for (int index = 0; index < 3; ++index)
        channel.observeMessage(groupMessage(10, 8823, "苹果", 1000 + index));
    for (int index = 0; index < 2; ++index)
        channel.observeMessage(groupMessage(10, 8823, "香蕉", 1100 + index));
    channel.observeMessage(groupMessage(10, 8823, "橘子", 1200));

    const auto topics = channel.hotTopics(8823, 2);
    ASSERT_EQ(topics.size(), 2U);
    EXPECT_EQ(topics[0].first, "苹果");
    EXPECT_EQ(topics[0].second, 3.0);
    EXPECT_EQ(topics[1].first, "香蕉");
    EXPECT_EQ(topics[1].second, 2.0);
}

TEST(PerceptionChannelTest, ExtractNgramsFiltersStopTerms)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    auto state = observingState(temporaryDirectory.path() + "/state.db");
    PerceptionChannel channel(state.get(), nullptr);

    // 高频功能词出现最多，但不是话题：不进热度
    for (int index = 0; index < 5; ++index)
        channel.observeMessage(groupMessage(10, 8823, "我们", 1000 + index));
    channel.observeMessage(groupMessage(10, 8823, "苹果", 2000));

    const auto topics = channel.hotTopics(8823, 10);
    for (const auto &topic : topics)
        EXPECT_NE(topic.first, "我们");
    ASSERT_EQ(topics.size(), 1U);
    EXPECT_EQ(topics[0].first, "苹果");
}

// 消费端：被 @ 时的话题注记——count≥2 门槛、关闭/无热点返回空、
// 格式为纯自然叙事（T6 教训：元词汇会被模型当成待应答的对话）
TEST(PerceptionChannelTest, TopicNoteGatesAndFormat)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());

    // 关闭：不生成注记
    auto disabledState = observingState(temporaryDirectory.path() + "/disabled.db", false);
    PerceptionChannel closed(disabledState.get(), nullptr);
    closed.observeMessage(groupMessage(10, 8823, "苹果", 1000));
    closed.observeMessage(groupMessage(10, 8823, "苹果", 1001));
    EXPECT_TRUE(closed.topicNoteFor(8823).empty());

    auto state = observingState(temporaryDirectory.path() + "/state.db");
    PerceptionChannel channel(state.get(), nullptr);

    // 只出现过一次的碎片：不算"常聊"，不给注记
    channel.observeMessage(groupMessage(10, 8823, "苹果", 1000));
    EXPECT_TRUE(channel.topicNoteFor(8823).empty());

    // 非白名单群：空
    channel.observeMessage(groupMessage(10, 9999, "香蕉", 1000));
    channel.observeMessage(groupMessage(10, 9999, "香蕉", 1001));
    EXPECT_TRUE(channel.topicNoteFor(9999).empty());

    // 重复出现：注记含碎片与最小标记，无"事件/任务"类元词汇
    channel.observeMessage(groupMessage(10, 8823, "苹果", 1001));
    channel.observeMessage(groupMessage(20, 8823, "香蕉", 1002));
    channel.observeMessage(groupMessage(20, 8823, "香蕉", 1003));
    const std::string note = channel.topicNoteFor(8823);
    ASSERT_FALSE(note.empty());
    EXPECT_NE(note.find("苹果"), std::string::npos);
    EXPECT_NE(note.find("香蕉"), std::string::npos);
    EXPECT_NE(note.find("群聊"), std::string::npos);
    EXPECT_EQ(note.find("事件"), std::string::npos);
    EXPECT_EQ(note.find("任务"), std::string::npos);
}

// 成本主张的证据（计划 §七）：1 万条消息观察 + flush 的耗时冒烟，
// 上界给到 10s——真实的量级是毫秒级，超限说明实现退化成了平方复杂度
TEST(PerceptionChannelTest, PerfSmokeTenThousandMessages)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    PerceptionStore store(temporaryDirectory.path() + "/conversation.db");
    auto state = observingState(temporaryDirectory.path() + "/state.db");
    PerceptionChannel channel(state.get(), &store);

    const auto started = std::chrono::steady_clock::now();
    for (int index = 0; index < 10000; ++index)
    {
        channel.observeMessage(groupMessage(10 + index % 20, 8823,
                                            "第" + std::to_string(index) + "条群聊内容",
                                            1000 + index));
    }
    channel.flushDue(100000);
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_LT(elapsed.count(), 10.0);
    EXPECT_EQ(readAffinityRows(temporaryDirectory.path() + "/conversation.db").size(), 20U);
}
