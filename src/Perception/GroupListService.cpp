#include "GroupListService.h"
#include "../Log/Log.h"

#include <algorithm>
#include <sqlite3.h>

namespace
{
// 刷新冷却：探测失败/OneBot 未就绪时的静默重试间隔（同 CapabilityBroker 模式）
constexpr std::int64_t kRetryCooldownSeconds = 60;
// 面板展示的群数上限防御（正常部署远小于此）
constexpr std::size_t kMaxGroups = 2000;

std::string columnText(sqlite3_stmt *statement, int column)
{
    const unsigned char *text = sqlite3_column_text(statement, column);
    return text ? reinterpret_cast<const char *>(text) : "";
}
}

GroupListService::GroupListService(const std::string &dbPath, OneBotApiChannel &api,
                                   const PerceptionOptions &options)
    : api(api), options(options)
{
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("群列表缓存库打开失败，面板将无法展示群选择器：" +
                  std::string(db ? sqlite3_errmsg(db) : "?"));
        if (db != nullptr)
            sqlite3_close(db);
        db = nullptr;
        return;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char *ddl =
        "CREATE TABLE IF NOT EXISTS group_cache ("
        " group_id INTEGER PRIMARY KEY,"
        " name TEXT NOT NULL DEFAULT '',"
        " member_count INTEGER NOT NULL DEFAULT 0,"
        " avatar_url TEXT NOT NULL DEFAULT '',"
        " monitored INTEGER NOT NULL DEFAULT 0,"
        " refreshed_ts INTEGER NOT NULL DEFAULT 0);";
    char *err = nullptr;
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &err) != SQLITE_OK)
    {
        LOG_ERROR("群列表缓存建表失败：" + std::string(err ? err : "?"));
        sqlite3_free(err);
        sqlite3_close(db);
        db = nullptr;
        return;
    }
    rebuildMirrorFromDisk();
}

GroupListService::~GroupListService()
{
    if (db != nullptr)
        sqlite3_close(db);
}

void GroupListService::rebuildMirrorFromDisk()
{
    std::vector<GroupListEntry> restored;
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "SELECT group_id, name, member_count, avatar_url, monitored,"
                           " refreshed_ts FROM group_cache ORDER BY member_count DESC;",
                           -1, &statement, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            GroupListEntry entry;
            entry.groupId = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 0));
            entry.name = columnText(statement, 1);
            entry.memberCount = sqlite3_column_int64(statement, 2);
            entry.avatarUrl = columnText(statement, 3);
            entry.monitored = sqlite3_column_int64(statement, 4) != 0;
            entry.refreshedTs = sqlite3_column_int64(statement, 5);
            restored.push_back(std::move(entry));
        }
        sqlite3_finalize(statement);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    mirror_ = std::move(restored);
}

std::string GroupListService::avatarUrlFor(std::uint64_t groupId)
{
    // qlogo 群头像规则 URL（各实现端 get_group_list 不返回头像，构造是通行做法）
    return "https://p.qlogo.cn/gh/" + std::to_string(groupId) + "/" +
           std::to_string(groupId) + "/640";
}

void GroupListService::poll(std::int64_t now)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ready_ || now < nextAttemptTs_)
            return;
        nextAttemptTs_ = now + kRetryCooldownSeconds;
    }

    const OneBotApiResult result = api.call("get_group_list", nlohmann::json::object(),
                                            std::chrono::seconds(5));
    if (result.networkError || result.retcode != 0 || !result.data.is_array())
        return; // OneBot 未就绪/不支持：保留旧缓存，冷却后重试

    std::vector<GroupListEntry> fetched;
    for (const auto &item : result.data)
    {
        if (!item.is_object() || !item.contains("group_id"))
            continue;
        GroupListEntry entry;
        entry.groupId = item.value("group_id", 0ULL);
        if (entry.groupId == 0)
            continue;
        entry.name = item.value("group_name", "");
        entry.memberCount = item.value("member_count", 0L);
        entry.avatarUrl = avatarUrlFor(entry.groupId);
        entry.refreshedTs = now;
        fetched.push_back(std::move(entry));
        if (fetched.size() >= kMaxGroups)
            break;
    }
    applyFetchedList(fetched, now);
}

void GroupListService::applyFetchedList(const std::vector<GroupListEntry> &fetched,
                                        std::int64_t now)
{
    if (fetched.empty())
        return;

    // monitored 是用户意志，刷新只覆写协议端数据列，勾选保留
    std::map<std::uint64_t, bool> previousMonitored;
    std::vector<GroupListEntry> merged = fetched;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const GroupListEntry &entry : mirror_)
            previousMonitored[entry.groupId] = entry.monitored;
        for (GroupListEntry &entry : merged)
        {
            const auto it = previousMonitored.find(entry.groupId);
            entry.monitored = it != previousMonitored.end() ? it->second
                                                            : entry.monitored;
        }
        mirror_ = merged;
        ready_ = true;
    }

    if (sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK)
        return;
    sqlite3_exec(db, "DELETE FROM group_cache;", nullptr, nullptr, nullptr);
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO group_cache"
                           " (group_id, name, member_count, avatar_url, monitored, refreshed_ts)"
                           " VALUES (?1,?2,?3,?4,?5,?6);",
                           -1, &statement, nullptr) == SQLITE_OK)
    {
        for (const GroupListEntry &entry : merged)
        {
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
            sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(entry.groupId));
            sqlite3_bind_text(statement, 2, entry.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(statement, 3, entry.memberCount);
            sqlite3_bind_text(statement, 4, entry.avatarUrl.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(statement, 5, entry.monitored ? 1 : 0);
            sqlite3_bind_int64(statement, 6, entry.refreshedTs);
            sqlite3_step(statement);
        }
        sqlite3_finalize(statement);
    }
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    LOG_INFO("群列表已刷新：" + std::to_string(merged.size()) + " 个群");
}

std::vector<GroupListEntry> GroupListService::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<GroupListEntry> result = mirror_;
    // monitored 实时对齐当前配置白名单（配置保存与缓存刷新两条写路，展示以配置为准）
    std::set<std::uint64_t> observing(options.observeGroups.begin(),
                                      options.observeGroups.end());
    for (GroupListEntry &entry : result)
        entry.monitored = observing.find(entry.groupId) != observing.end();
    std::sort(result.begin(), result.end(),
              [](const GroupListEntry &left, const GroupListEntry &right) {
                  return left.memberCount > right.memberCount;
              });
    return result;
}

void GroupListService::onOptionsChanged(const PerceptionOptions &options)
{
    std::lock_guard<std::mutex> lock(mutex_);
    this->options = options;
}
