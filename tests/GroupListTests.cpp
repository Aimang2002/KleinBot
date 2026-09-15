#include <gtest/gtest.h>

#include "Network/OneBotApiChannel.h"
#include "Perception/GroupListService.h"

#include <filesystem>
#include <sqlite3.h>
#include <string>
#include <utility>
#include <vector>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-grouplist-XXXXXX";
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

class FakeApiChannel final : public OneBotApiChannel
{
public:
    OneBotApiResult call(const std::string &action, nlohmann::json params,
                         std::chrono::milliseconds) override
    {
        (void)params;
        actions.push_back(action);
        return result;
    }

    std::vector<std::string> actions;
    OneBotApiResult result;
};

nlohmann::json twoGroups()
{
    return nlohmann::json::array({
        {{"group_id", 8823}, {"group_name", "原神交流群"}, {"member_count", 420}},
        {{"group_id", 9001}, {"group_name", "闲聊群"}, {"member_count", 88}},
    });
}

const GroupListEntry *findEntry(const std::vector<GroupListEntry> &entries, std::uint64_t groupId)
{
    for (const GroupListEntry &entry : entries)
    {
        if (entry.groupId == groupId)
            return &entry;
    }
    return nullptr;
}

// 直接读库里的 monitored 0/1 布尔值
std::vector<std::pair<std::uint64_t, int>> readMonitoredFlags(const std::string &dbPath)
{
    std::vector<std::pair<std::uint64_t, int>> flags;
    sqlite3 *database = nullptr;
    if (sqlite3_open(dbPath.c_str(), &database) != SQLITE_OK)
    {
        sqlite3_close(database);
        return flags;
    }
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(database,
                           "SELECT group_id, monitored FROM group_cache ORDER BY group_id;",
                           -1, &statement, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            flags.emplace_back(
                static_cast<std::uint64_t>(sqlite3_column_int64(statement, 0)),
                static_cast<int>(sqlite3_column_int64(statement, 1)));
        }
        sqlite3_finalize(statement);
    }
    sqlite3_close(database);
    return flags;
}

int monitoredOf(const std::vector<std::pair<std::uint64_t, int>> &flags, std::uint64_t groupId)
{
    for (const auto &entry : flags)
    {
        if (entry.first == groupId)
            return entry.second;
    }
    return -1; // 无行
}

std::string readMeta(const std::string &dbPath, const std::string &key)
{
    sqlite3 *database = nullptr;
    if (sqlite3_open(dbPath.c_str(), &database) != SQLITE_OK)
    {
        sqlite3_close(database);
        return {};
    }
    sqlite3_stmt *statement = nullptr;
    std::string value;
    if (sqlite3_prepare_v2(database, "SELECT value FROM perception_meta WHERE key=?1;", -1,
                           &statement, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(statement, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(statement) == SQLITE_ROW &&
            sqlite3_column_text(statement, 0) != nullptr)
            value = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
        sqlite3_finalize(statement);
    }
    sqlite3_close(database);
    return value;
}
} // namespace

// 群列表拉取：冷却门控、协议端字段、日级降频、重启重建
TEST(GroupListServiceTest, FetchMergesMonitoredAndPersistsAcrossRestart)
{
    TemporaryDirectory temporaryDirectory;
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    FakeApiChannel api;
    api.result.retcode = 0;
    api.result.data = twoGroups();

    {
        GroupListService service(dbPath, api);
        ASSERT_TRUE(service.isOpen());
        EXPECT_TRUE(service.snapshot().empty()) << "首次启动：磁盘无缓存";

        // 首次 poll：OneBot 未就绪（networkError）→ 冷却保留，不发第二次
        api.result.networkError = true;
        service.poll(1000);
        service.poll(1030); // 30s 后：仍在 60s 冷却内，不发第二次
        EXPECT_EQ(api.actions.size(), 1U);
        EXPECT_EQ(api.actions[0], "get_group_list");

        // 监控其一（状态入库），冷却过后再拉取
        EXPECT_TRUE(service.setMonitored(8823, true));
        api.result.networkError = false;
        service.poll(1000 + 61);
        EXPECT_EQ(api.actions.size(), 2U);

        auto entries = service.snapshot();
        ASSERT_EQ(entries.size(), 2U);
        const GroupListEntry *monitored = findEntry(entries, 8823);
        ASSERT_NE(monitored, nullptr);
        EXPECT_TRUE(monitored->monitored);
        EXPECT_EQ(monitored->name, "原神交流群");
        EXPECT_EQ(monitored->memberCount, 420);
        EXPECT_NE(monitored->avatarUrl.find("p.qlogo.cn/gh/8823"), std::string::npos);
        const GroupListEntry *unmonitored = findEntry(entries, 9001);
        ASSERT_NE(unmonitored, nullptr);
        EXPECT_FALSE(unmonitored->monitored);

        // 就绪后降频：短时间内不再拉取
        service.poll(2000);
        EXPECT_EQ(api.actions.size(), 2U);
    }

    // 重启：镜像与监控集都从磁盘重建（OneBot 未连上也有数据）
    {
        FakeApiChannel offlineApi;
        offlineApi.result.networkError = true;
        GroupListService service(dbPath, offlineApi);
        auto entries = service.snapshot();
        ASSERT_EQ(entries.size(), 2U);
        const GroupListEntry *monitored = findEntry(entries, 8823);
        ASSERT_NE(monitored, nullptr);
        EXPECT_TRUE(monitored->monitored);
        EXPECT_EQ(monitored->name, "原神交流群");
        EXPECT_TRUE(service.isMonitored(8823));
        EXPECT_FALSE(service.isMonitored(9001));
    }
}

// 监控状态是数据库真值：切换即刻生效于读路径与落库（无需重启）
TEST(GroupListServiceTest, MonitorToggleIsDatabaseBackedAndImmediate)
{
    TemporaryDirectory temporaryDirectory;
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    FakeApiChannel api;
    api.result.retcode = 0;
    api.result.data = twoGroups();
    GroupListService service(dbPath, api);
    service.poll(1000);

    EXPECT_TRUE(service.setMonitored(9001, true));
    {
        auto entries = service.snapshot();
        EXPECT_FALSE(findEntry(entries, 8823)->monitored);
        EXPECT_TRUE(findEntry(entries, 9001)->monitored);
        EXPECT_TRUE(service.isMonitored(9001));
        EXPECT_EQ(service.monitoredCount(), 1U);
    }
    {
        const auto flags = readMonitoredFlags(dbPath);
        EXPECT_EQ(monitoredOf(flags, 8823), 0);
        EXPECT_EQ(monitoredOf(flags, 9001), 1);
    }

    // 取消监控：读路径与库同步翻转
    EXPECT_TRUE(service.setMonitored(9001, false));
    EXPECT_FALSE(service.isMonitored(9001));
    EXPECT_EQ(service.monitoredCount(), 0U);
    for (const auto &flag : readMonitoredFlags(dbPath))
        EXPECT_EQ(flag.second, 0);

    // 不在群列表中的群号（手动输入）也能被监控：补行保存意图
    EXPECT_TRUE(service.setMonitored(7777, true));
    EXPECT_TRUE(service.isMonitored(7777));
    EXPECT_EQ(monitoredOf(readMonitoredFlags(dbPath), 7777), 1);
}

// 刷新（协议端整表覆写）不得丢失监控状态，含不在群列表中的手动群号
TEST(GroupListServiceTest, RefreshPreservesMonitoredState)
{
    TemporaryDirectory temporaryDirectory;
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    FakeApiChannel api;
    api.result.retcode = 0;
    api.result.data = twoGroups();
    GroupListService service(dbPath, api);
    service.poll(1000);
    service.setMonitored(8823, true);
    service.setMonitored(7777, true); // 机器人不在该群

    // 模拟换机器人后重拉：只剩 8823 与另一个新群
    api.result.data = nlohmann::json::array({
        {{"group_id", 8823}, {"group_name", "改名的群"}, {"member_count", 500}},
        {{"group_id", 5555}, {"group_name", "新群"}, {"member_count", 30}},
    });
    {
        GroupListService reloaded(dbPath, api);
        reloaded.poll(2000);
        auto entries = reloaded.snapshot();
        const GroupListEntry *kept = findEntry(entries, 8823);
        ASSERT_NE(kept, nullptr);
        EXPECT_EQ(kept->name, "改名的群") << "协议端数据被覆写";
        EXPECT_TRUE(kept->monitored) << "监控状态不被刷新冲掉";
        EXPECT_TRUE(reloaded.isMonitored(7777)) << "不在列表中的手动群号仍受监控";
        EXPECT_FALSE(findEntry(entries, 5555)->monitored);
    }
}

// 总开关：0/1 入库，默认关闭，重启恢复
TEST(GroupListServiceTest, FeatureSwitchPersistedAsBoolean)
{
    TemporaryDirectory temporaryDirectory;
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    FakeApiChannel api;
    {
        GroupListService service(dbPath, api);
        EXPECT_FALSE(service.featureEnabled()) << "默认关闭（无历史状态）";
        EXPECT_TRUE(service.setFeatureEnabled(true));
        EXPECT_TRUE(service.featureEnabled());
    }
    {
        GroupListService service(dbPath, api);
        EXPECT_TRUE(service.featureEnabled()) << "重启后开关状态从库恢复";
        EXPECT_EQ(readMeta(dbPath, "feature_enabled"), "1");
        EXPECT_TRUE(service.setFeatureEnabled(false));
        EXPECT_FALSE(service.featureEnabled());
    }
    {
        GroupListService service(dbPath, api);
        EXPECT_FALSE(service.featureEnabled());
        EXPECT_EQ(readMeta(dbPath, "feature_enabled"), "0");
    }
}
