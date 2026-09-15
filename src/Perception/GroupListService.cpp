#include "GroupListService.h"
#include "../Log/Log.h"

#include <algorithm>
#include <sqlite3.h>

namespace
{
// 刷新冷却：探测失败/OneBot 未就绪时的静默重试间隔（同 CapabilityBroker 模式）；
// 已就绪后降频为日级刷新（换机器人/群变动最终一致）
constexpr std::int64_t kRetryCooldownSeconds = 60;
constexpr std::int64_t kRefreshIntervalSeconds = 24 * 60 * 60;
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
        " monitored INTEGER NOT NULL DEFAULT 0,"   // 0/1：1=该群在观察白名单内
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
    // 启动即把配置白名单落到 monitored 列：库内 0/1 与 .config.json 一致
    applyWhitelist(options.observeGroups);
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
    // qlogo 群头像规则 URL（各实现端 get_group_list 不返回头像，构造是通行做法）。
    // 用 /100 而非 /640：面板展示尺寸只有 20~28px，640 原图约 9KB、100 约 1.6KB，
    // 群多时差别可观
    return "https://p.qlogo.cn/gh/" + std::to_string(groupId) + "/" +
           std::to_string(groupId) + "/100";
}

void GroupListService::poll(std::int64_t now)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (now < nextAttemptTs_)
            return;
        nextAttemptTs_ = now + (ready_ ? kRefreshIntervalSeconds : kRetryCooldownSeconds);
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

    // monitored 由配置白名单派生（唯一真值来源）：刷新只覆写协议端数据列，
    // 监控标记重新按当前白名单标注——不能"保留旧值"，因为首次拉取时镜像
    // 还是空的，白名单标注会丢失
    std::vector<GroupListEntry> merged = fetched;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::set<std::uint64_t> observing(options.observeGroups.begin(),
                                                options.observeGroups.end());
        for (GroupListEntry &entry : merged)
            entry.monitored = observing.find(entry.groupId) != observing.end();
        mirror_ = merged;
        ready_ = true;
        nextAttemptTs_ = now + kRefreshIntervalSeconds; // 成功后降频为日级刷新
        persistMonitoredUnderLock();
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

void GroupListService::applyWhitelist(const std::vector<std::uint64_t> &observeGroups)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        options.observeGroups = observeGroups;
        const std::set<std::uint64_t> observing(observeGroups.begin(), observeGroups.end());
        for (GroupListEntry &entry : mirror_)
            entry.monitored = observing.find(entry.groupId) != observing.end();
        persistMonitoredUnderLock();
    }
}

void GroupListService::persistMonitoredUnderLock()
{
    if (db == nullptr)
        return;
    // 全量归零 + 白名单置 1（幂等）：monitored 是 0/1 布尔，
    // 白名单群不在 group_cache 里（机器人不在该群）时无行可写，
    // 只影响落盘副本，不影响实际观察
    if (sqlite3_exec(db, "UPDATE group_cache SET monitored = 0;", nullptr, nullptr,
                     nullptr) != SQLITE_OK)
    {
        LOG_ERROR("group_cache.monitored 归零失败：" + std::string(sqlite3_errmsg(db)));
        return;
    }
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, "UPDATE group_cache SET monitored = 1 WHERE group_id = ?1;",
                           -1, &statement, nullptr) != SQLITE_OK)
        return;
    int marked = 0;
    for (const GroupListEntry &entry : mirror_)
    {
        if (!entry.monitored)
            continue;
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(entry.groupId));
        if (sqlite3_step(statement) == SQLITE_DONE)
            marked += sqlite3_changes(db);
    }
    sqlite3_finalize(statement);
    LOG_INFO("观察白名单已落库：monitored=1 的群 " + std::to_string(marked) + " 个");
}

std::vector<GroupListEntry> GroupListService::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<GroupListEntry> result = mirror_;
    // monitored 以当前白名单为准（与落库副本一致；配置是唯一真值来源）
    const std::set<std::uint64_t> observing(options.observeGroups.begin(),
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
    applyWhitelist(options.observeGroups);
}
