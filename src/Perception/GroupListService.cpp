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

// perception_meta 键名（观察通道运行时状态）
constexpr const char *kFeatureEnabledKey = "feature_enabled";

std::string columnText(sqlite3_stmt *statement, int column)
{
    const unsigned char *text = sqlite3_column_text(statement, column);
    return text ? reinterpret_cast<const char *>(text) : "";
}
}

GroupListService::GroupListService(const std::string &dbPath, OneBotApiChannel &api)
    : api(api)
{
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("观察状态库打开失败，观察通道与面板群列表不可用：" +
                  std::string(db ? sqlite3_errmsg(db) : "?"));
        if (db != nullptr)
            sqlite3_close(db);
        db = nullptr;
        return;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char *ddl =
        "CREATE TABLE IF NOT EXISTS perception_meta ("
        " key TEXT PRIMARY KEY,"
        " value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS group_cache ("
        " group_id INTEGER PRIMARY KEY,"
        " name TEXT NOT NULL DEFAULT '',"
        " member_count INTEGER NOT NULL DEFAULT 0,"
        " avatar_url TEXT NOT NULL DEFAULT '',"
        " monitored INTEGER NOT NULL DEFAULT 0," // 0/1：1=监控该群
        " refreshed_ts INTEGER NOT NULL DEFAULT 0);";
    char *err = nullptr;
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &err) != SQLITE_OK)
    {
        LOG_ERROR("观察状态库建表失败：" + std::string(err ? err : "?"));
        sqlite3_free(err);
        sqlite3_close(db);
        db = nullptr;
        return;
    }
    loadStateFromDisk();
    rebuildMirrorFromDisk();
    LOG_INFO("观察通道状态（数据库）：" + std::string(enabled_ ? "已启用" : "未启用") +
             "，监控群 " + std::to_string(monitored_.size()) + " 个");
}

GroupListService::~GroupListService()
{
    if (db != nullptr)
        sqlite3_close(db);
}

std::string GroupListService::readMeta(const std::string &key) const
{
    if (db == nullptr)
        return {};
    sqlite3_stmt *statement = nullptr;
    std::string value;
    if (sqlite3_prepare_v2(db, "SELECT value FROM perception_meta WHERE key=?1;", -1,
                           &statement, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(statement, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(statement) == SQLITE_ROW)
            value = columnText(statement, 0);
        sqlite3_finalize(statement);
    }
    return value;
}

bool GroupListService::writeMeta(const std::string &key, const std::string &value)
{
    if (db == nullptr)
        return false;
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO perception_meta(key, value) VALUES (?1, ?2)"
                           " ON CONFLICT(key) DO UPDATE SET value = excluded.value;",
                           -1, &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(statement, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok)
        LOG_ERROR("观察状态写入失败：" + std::string(sqlite3_errmsg(db)));
    return ok;
}

void GroupListService::loadStateFromDisk()
{
    const std::string flag = readMeta(kFeatureEnabledKey);
    enabled_ = flag == "1";

    // 监控群集（含未在群列表中的群号——手动输入也据此持久化）
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT group_id FROM group_cache WHERE monitored=1;", -1,
                           &statement, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(statement) == SQLITE_ROW)
            monitored_.insert(static_cast<std::uint64_t>(sqlite3_column_int64(statement, 0)));
        sqlite3_finalize(statement);
    }
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
            entry.memberCount = sqlite3_column_int(statement, 2);
            entry.avatarUrl = columnText(statement, 3);
            entry.monitored = sqlite3_column_int(statement, 4) != 0;
            entry.refreshedTs = sqlite3_column_int64(statement, 5);
            restored.push_back(std::move(entry));
        }
        sqlite3_finalize(statement);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    mirror_ = std::move(restored);
}

bool GroupListService::featureEnabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

bool GroupListService::setFeatureEnabled(bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enabled_ == enabled)
            return true;
        enabled_ = enabled;
        if (!writeMeta(kFeatureEnabledKey, enabled ? "1" : "0"))
            return false;
    }
    LOG_INFO(std::string("观察通道总开关已") + (enabled ? "开启" : "关闭") +
             "（数据库生效，无需重启）");
    return true;
}

bool GroupListService::isMonitored(std::uint64_t groupId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return monitored_.find(groupId) != monitored_.end();
}

bool GroupListService::setMonitored(std::uint64_t groupId, bool monitored)
{
    if (db == nullptr || groupId == 0)
        return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool already = monitored_.find(groupId) != monitored_.end();
        if (already == monitored)
            return true;
        if (monitored)
        {
            monitored_.insert(groupId);
            // 群列表里没有该群号（机器人不在群里/尚未拉取）也要落行，
            // 否则监控意图无处保存；元数据留给后续刷新填充
            sqlite3_exec(db,
                         ("INSERT OR IGNORE INTO group_cache (group_id, avatar_url)"
                          " VALUES (" + std::to_string(groupId) + ", '" +
                          avatarUrlFor(groupId) + "');").c_str(),
                         nullptr, nullptr, nullptr);
        }
        else
        {
            monitored_.erase(groupId);
        }
        for (GroupListEntry &entry : mirror_)
        {
            if (entry.groupId == groupId)
                entry.monitored = monitored;
        }
        persistMonitoredUnderLock();
    }
    LOG_INFO("群 " + std::to_string(groupId) + (monitored ? " 已开启监控" : " 已关闭监控") +
             "（数据库生效，无需重启）");
    return true;
}

std::size_t GroupListService::monitoredCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return monitored_.size();
}

std::vector<std::uint64_t> GroupListService::monitoredGroups() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return {monitored_.begin(), monitored_.end()};
}

void GroupListService::persistEnabledUnderLock()
{
    writeMeta(kFeatureEnabledKey, enabled_ ? "1" : "0");
}

void GroupListService::persistMonitoredUnderLock()
{
    if (db == nullptr)
        return;
    // 全量归零 + 监控集置 1（幂等）：monitored 是 0/1 布尔
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
    for (std::uint64_t groupId : monitored_)
    {
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(groupId));
        sqlite3_step(statement);
    }
    sqlite3_finalize(statement);
}

std::string GroupListService::avatarUrlFor(std::uint64_t groupId)
{
    // qlogo 群头像规则 URL（各实现端 get_group_list 不返回头像，构造是通行做法）。
    // 用 /100 而非 /640：面板展示尺寸只有 20~28px，640 原图约 9KB、100 约 1.6KB
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

    std::vector<GroupListEntry> merged = fetched;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 协议端数据整表覆写，monitored 由监控集派生（唯一真值），
        // 刷新不得丢失监控状态——含未在列表中的手动群号（补回镜像）
        for (GroupListEntry &entry : merged)
            entry.monitored = monitored_.find(entry.groupId) != monitored_.end();

        std::set<std::uint64_t> present;
        for (const GroupListEntry &entry : merged)
            present.insert(entry.groupId);
        for (std::uint64_t groupId : monitored_)
        {
            if (present.find(groupId) != present.end())
                continue;
            for (const GroupListEntry &existing : mirror_)
            {
                if (existing.groupId != groupId)
                    continue;
                merged.push_back(existing); // 手动加入但不在机器人群列表里的群号
                break;
            }
        }
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

std::vector<GroupListEntry> GroupListService::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<GroupListEntry> result = mirror_;
    for (GroupListEntry &entry : result)
        entry.monitored = monitored_.find(entry.groupId) != monitored_.end();
    std::sort(result.begin(), result.end(),
              [](const GroupListEntry &left, const GroupListEntry &right) {
                  return left.memberCount > right.memberCount;
              });
    return result;
}
