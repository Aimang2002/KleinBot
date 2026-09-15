#include <gtest/gtest.h>

#include "Network/OneBotApiChannel.h"
#include "Perception/GroupListService.h"

#include <filesystem>
#include <sqlite3.h>
#include <string>
#include <utility>

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

PerceptionOptions whitelistOptions()
{
    PerceptionOptions options;
    options.enabled = true;
    options.observeGroups = {8823};
    return options;
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
} // namespace

TEST(GroupListServiceTest, FetchMergesMonitoredAndPersistsAcrossRestart)
{
    TemporaryDirectory temporaryDirectory;
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    FakeApiChannel api;
    api.result.retcode = 0;
    api.result.data = nlohmann::json::array({
        {{"group_id", 8823}, {"group_name", "原神交流群"}, {"member_count", 420}},
        {{"group_id", 9001}, {"group_name", "闲聊群"}, {"member_count", 88}},
    });

    {
        GroupListService service(dbPath, api, whitelistOptions());
        ASSERT_TRUE(service.isOpen());
        EXPECT_TRUE(service.snapshot().empty()) << "OneBot 未就绪时镜像只有磁盘旧缓存";

        // 首次 poll：OneBot 未就绪（networkError）→ 冷却保留，不发第二次
        api.result.networkError = true;
        service.poll(1000);
        service.poll(1030); // 30s 后：仍在 60s 冷却内，不发第二次
        EXPECT_EQ(api.actions.size(), 1U);
        EXPECT_EQ(api.actions[0], "get_group_list");

        // 冷却后 OneBot 就绪：拉到两个群；8823 在配置白名单 → monitored
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
        // 就绪后降频：2 秒后不拉（日级刷新间隔），冷却语义保留
        service.poll(2000);
        EXPECT_EQ(api.actions.size(), 2U);
    }

    // 重启：镜像从磁盘重建（OneBot 未连上也有数据），monitored 随行
    {
        FakeApiChannel offlineApi;
        offlineApi.result.networkError = true;
        GroupListService service(dbPath, offlineApi, whitelistOptions());
        auto entries = service.snapshot();
        ASSERT_EQ(entries.size(), 2U);
        const GroupListEntry *monitored = findEntry(entries, 8823);
        ASSERT_NE(monitored, nullptr);
        EXPECT_TRUE(monitored->monitored);
        EXPECT_EQ(monitored->name, "原神交流群");
    }
}

TEST(GroupListServiceTest, ConfigWhitelistDrivesMonitoredAnnotation)
{
    TemporaryDirectory temporaryDirectory;
    FakeApiChannel api;
    api.result.retcode = 0;
    api.result.data = nlohmann::json::array({
        {{"group_id", 8823}, {"group_name", "A群"}, {"member_count", 10}},
        {{"group_id", 9001}, {"group_name", "B群"}, {"member_count", 20}},
    });
    GroupListService service(temporaryDirectory.path() + "/conversation.db", api,
                             whitelistOptions());
    service.poll(1000);

    // 配置白名单变化（面板保存后组合根通知）：monitored 标注实时对齐
    PerceptionOptions updated = whitelistOptions();
    updated.observeGroups = {9001};
    service.onOptionsChanged(updated);

    auto entries = service.snapshot();
    EXPECT_FALSE(findEntry(entries, 8823)->monitored);
    EXPECT_TRUE(findEntry(entries, 9001)->monitored);

    // 快照按人数降序（前端列表稳定）
    EXPECT_EQ(entries[0].groupId, 9001U);
    EXPECT_EQ(entries[1].groupId, 8823U);
}

TEST(GroupListServiceTest, RefreshPreservesMonitoredEvenOffWhitelist)
{
    TemporaryDirectory temporaryDirectory;
    FakeApiChannel api;
    api.result.retcode = 0;
    api.result.data = nlohmann::json::array({
        {{"group_id", 8823}, {"group_name", "A群"}, {"member_count", 10}},
    });
    GroupListService service(temporaryDirectory.path() + "/conversation.db", api,
                             whitelistOptions());
    service.poll(1000);
    auto firstSnapshot = service.snapshot();
    EXPECT_TRUE(findEntry(firstSnapshot, 8823)->monitored);

    // 二次刷新（模拟换机器人后重拉）：协议端数据覆写，monitored 不被清零——
    // 配置白名单仍含 8823，标注依然为 true
    api.result.data = nlohmann::json::array({
        {{"group_id", 8823}, {"group_name", "A群改名了"}, {"member_count", 15}},
    });
    // ready_ 后 poll 不再拉取；直接通过新实例验证覆写保留逻辑
    {
        GroupListService reloaded(temporaryDirectory.path() + "/conversation.db", api,
                                  whitelistOptions());
        reloaded.poll(2000);
        const auto snapshot = reloaded.snapshot();
        const GroupListEntry *entry = findEntry(snapshot, 8823);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->name, "A群改名了");
        EXPECT_EQ(entry->memberCount, 15);
        EXPECT_TRUE(entry->monitored) << "刷新覆写协议端数据但保留用户勾选";
    }
}

namespace
{
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
}

// monitored 落库语义（用户定规）：0/1 布尔，1 = 该群在观察白名单内。
// 启动时按配置白名单写入，面板保存后同步——不再是无信息的死列
TEST(GroupListServiceTest, MonitoredFlagPersistedAsBoolean)
{
    TemporaryDirectory temporaryDirectory;
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    FakeApiChannel api;
    api.result.retcode = 0;
    api.result.data = nlohmann::json::array({
        {{"group_id", 8823}, {"group_name", "白名单群"}, {"member_count", 10}},
        {{"group_id", 9001}, {"group_name", "普通群"}, {"member_count", 20}},
    });

    {
        GroupListService service(dbPath, api, whitelistOptions()); // 白名单 = {8823}
        service.poll(1000);
        auto flags = readMonitoredFlags(dbPath);
        ASSERT_EQ(flags.size(), 2U);
        EXPECT_EQ(monitoredOf(flags, 8823), 1) << "白名单群 monitored=1（开启监控）";
        EXPECT_EQ(monitoredOf(flags, 9001), 0) << "非白名单群 monitored=0";
    }

    // 面板保存后切白名单：落库值随之翻转
    {
        GroupListService service(dbPath, api, whitelistOptions());
        PerceptionOptions updated;
        updated.enabled = true;
        updated.observeGroups = {9001};
        service.applyWhitelist(updated.observeGroups);

        const auto flags = readMonitoredFlags(dbPath);
        EXPECT_EQ(monitoredOf(flags, 8823), 0);
        EXPECT_EQ(monitoredOf(flags, 9001), 1);
        // 面板读路径与落库一致
        for (const GroupListEntry &entry : service.snapshot())
            EXPECT_EQ(entry.monitored, entry.groupId == 9001U);
    }

    // 清空白名单：全 0
    {
        GroupListService service(dbPath, api, whitelistOptions());
        service.applyWhitelist({});
        for (const auto &flag : readMonitoredFlags(dbPath))
            EXPECT_EQ(flag.second, 0);
    }
}
