#include "GroupContextStore.h"
#include "../Log/Log.h"
#include "../utils/FsUtil.h"

#include <sqlite3.h>

#include <algorithm>

namespace
{
// 双上限（用户定规 2026-09-15）：每群最近 300 条 + 24 小时 TTL
constexpr std::size_t kMaxMessagesPerGroup = 300;
constexpr std::int64_t kTtlSeconds = 24 * 60 * 60;

void deleteBeforeSeq(sqlite3 *db, std::uint64_t groupId, std::int64_t seq)
{
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "DELETE FROM group_messages WHERE group_id=?1 AND seq<?2;",
                           -1, &statement, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(groupId));
        sqlite3_bind_int64(statement, 2, seq);
        sqlite3_step(statement);
        sqlite3_finalize(statement);
    }
}
}

GroupContextStore::GroupContextStore(const std::string &dbPath)
{
    utils::ensureParentDirectories(dbPath);
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("群内容库打开失败，群上下文仅存内存（重启即失）：" +
                  std::string(db ? sqlite3_errmsg(db) : "?"));
        if (db != nullptr)
            sqlite3_close(db);
        db = nullptr;
        return;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    // 旧版本以加盐哈希伪名列 speaker_id 存发言人（T7b 的伪名化）——哈希不可逆、
    // 旧行无法还原为 QQ，且该结构从未发布（用户定规 2026-09-15 改存原始 QQ），
    // 检测到即整表重建（群内容本身是 24h 短时缓冲，成本可忽略）
    {
        sqlite3_stmt *probe = nullptr;
        bool legacy = false;
        if (sqlite3_prepare_v2(db, "PRAGMA table_info(group_messages);", -1, &probe,
                              nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(probe) == SQLITE_ROW)
            {
                const unsigned char *name = sqlite3_column_text(probe, 1);
                if (name != nullptr &&
                    std::string(reinterpret_cast<const char *>(name)) == "speaker_id")
                    legacy = true;
            }
            sqlite3_finalize(probe);
        }
        if (legacy)
        {
            LOG_WARNING("group_messages 旧伪名化结构已废弃（改存原始 QQ），整表重建");
            sqlite3_exec(db, "DROP TABLE IF EXISTS group_messages;", nullptr, nullptr, nullptr);
        }
    }

    const char *ddl =
        "CREATE TABLE IF NOT EXISTS perception_meta ("
        " key TEXT PRIMARY KEY,"
        " value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS group_messages ("
        " seq INTEGER PRIMARY KEY,"
        " group_id INTEGER NOT NULL,"
        " user_id INTEGER NOT NULL,"
        " nickname TEXT NOT NULL,"
        " text TEXT NOT NULL,"
        " ts INTEGER NOT NULL,"
        " mentions_bot INTEGER NOT NULL DEFAULT 0,"
        " is_self INTEGER NOT NULL DEFAULT 0,"
        " message_id TEXT NOT NULL DEFAULT '',"
        " reply_to_message_id TEXT NOT NULL DEFAULT '');"
        "CREATE INDEX IF NOT EXISTS idx_group_messages_group"
        " ON group_messages(group_id, seq);"
        "CREATE TABLE IF NOT EXISTS engagement_cold ("
        " group_id INTEGER PRIMARY KEY,"
        " hourly_ema REAL NOT NULL DEFAULT -1,"
        " armed_ts INTEGER NOT NULL DEFAULT 0,"
        " day_anchor INTEGER NOT NULL DEFAULT 0,"
        " cold_today INTEGER NOT NULL DEFAULT 0,"
        " cooldown_until INTEGER NOT NULL DEFAULT 0);";
    char *err = nullptr;
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &err) != SQLITE_OK)
    {
        LOG_ERROR("群内容库建表失败，群上下文仅存内存：" + std::string(err ? err : "?"));
        sqlite3_free(err);
        sqlite3_close(db);
        db = nullptr;
        return;
    }

    // 冷启动重建镜像：按 seq 升序读回全部存活行，nextSeq 接续
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "SELECT seq, group_id, user_id, nickname, text, ts,"
                           " mentions_bot, is_self, message_id, reply_to_message_id"
                           " FROM group_messages ORDER BY seq;",
                           -1, &statement, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            GroupMessageRecord record;
            record.seq = sqlite3_column_int64(statement, 0);
            record.groupId = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 1));
            record.userId = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 2));
            record.nickname = reinterpret_cast<const char *>(sqlite3_column_text(statement, 3));
            record.text = reinterpret_cast<const char *>(sqlite3_column_text(statement, 4));
            record.timestamp = sqlite3_column_int64(statement, 5);
            record.mentionsBot = sqlite3_column_int64(statement, 6) != 0;
            record.isSelf = sqlite3_column_int64(statement, 7) != 0;
            record.messageId = reinterpret_cast<const char *>(sqlite3_column_text(statement, 8));
            record.replyToMessageId = reinterpret_cast<const char *>(sqlite3_column_text(statement, 9));
            mirrors[record.groupId].push_back(std::move(record));
            nextSeq = std::max(nextSeq, record.seq + 1);
        }
        sqlite3_finalize(statement);
        LOG_INFO("群内容库镜像重建：群 " + std::to_string(mirrors.size()) + " 个");
    }
}

GroupContextStore::~GroupContextStore()
{
    if (db != nullptr)
        sqlite3_close(db);
}

std::int64_t GroupContextStore::append(const GroupMessageRecord &record)
{
    std::int64_t seq = 0;
    bool overflowed = false;
    std::uint64_t evictedGroup = 0;
    std::int64_t evictedSeq = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        seq = nextSeq++;
        auto &mirror = mirrors[record.groupId];
        mirror.push_back(record);
        mirror.back().seq = seq;
        if (mirror.size() > kMaxMessagesPerGroup)
        {
            evictedGroup = record.groupId;
            evictedSeq = mirror.front().seq;
            mirror.pop_front();
            overflowed = true;
        }
    }

    // 落库与淘汰在锁外：SQLite IO 不阻塞观察路径
    if (db != nullptr)
    {
        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(db,
                               "INSERT INTO group_messages"
                               " (seq, group_id, user_id, nickname, text, ts,"
                               "  mentions_bot, is_self, message_id, reply_to_message_id)"
                               " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10);",
                               -1, &statement, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int64(statement, 1, seq);
            sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(record.groupId));
            sqlite3_bind_int64(statement, 3, static_cast<sqlite3_int64>(record.userId));
            sqlite3_bind_text(statement, 4, record.nickname.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 5, record.text.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(statement, 6, record.timestamp);
            sqlite3_bind_int64(statement, 7, record.mentionsBot ? 1 : 0);
            sqlite3_bind_int64(statement, 8, record.isSelf ? 1 : 0);
            sqlite3_bind_text(statement, 9, record.messageId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 10, record.replyToMessageId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(statement) != SQLITE_DONE)
                LOG_ERROR("群内容写入失败：" + std::string(sqlite3_errmsg(db)));
            sqlite3_finalize(statement);
        }
        if (overflowed)
            deleteBeforeSeq(db, evictedGroup, evictedSeq);
    }
    return seq;
}

void GroupContextStore::prune(std::int64_t now)
{
    // TTL：库级一次删除 + 镜像同步收缩。条数淘汰已在 append 内逐群处理
    const std::int64_t horizon = now - kTtlSeconds;

    std::vector<std::pair<std::uint64_t, std::int64_t>> evictions;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto &[groupId, mirror] : mirrors)
        {
            while (!mirror.empty() && mirror.front().timestamp < horizon)
            {
                evictions.emplace_back(groupId, mirror.front().seq);
                mirror.pop_front();
            }
        }
    }
    if (db == nullptr || evictions.empty())
        return;

    // 按群归并成区间边界：删除每群被逐出的最大 seq 之前（append 的条数淘汰
    // 保证 seq 严格时间序，区间删除等价且语句数 = 群数）
    std::map<std::uint64_t, std::int64_t> boundary;
    for (const auto &[groupId, seq] : evictions)
        boundary[groupId] = std::max(boundary[groupId], seq);
    for (const auto &[groupId, seq] : boundary)
        deleteBeforeSeq(db, groupId, seq + 1);
}

std::vector<GroupMessageRecord> GroupContextStore::snapshot(std::uint64_t groupId) const
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = mirrors.find(groupId);
    if (it == mirrors.end())
        return {};
    return {it->second.begin(), it->second.end()};
}

std::map<std::uint64_t, EngagementColdRow> GroupContextStore::loadEngagementCold() const
{
    std::map<std::uint64_t, EngagementColdRow> result;
    if (db == nullptr)
        return result;
    std::lock_guard<std::mutex> lock(mutex);
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "SELECT group_id, hourly_ema, armed_ts, day_anchor,"
                           " cold_today, cooldown_until FROM engagement_cold;",
                           -1, &statement, nullptr) != SQLITE_OK)
        return result;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        EngagementColdRow row;
        const std::uint64_t groupId =
            static_cast<std::uint64_t>(sqlite3_column_int64(statement, 0));
        row.hourlyEma = sqlite3_column_double(statement, 1);
        row.armedTs = sqlite3_column_int64(statement, 2);
        row.dayAnchor = sqlite3_column_int64(statement, 3);
        row.coldToday = sqlite3_column_int(statement, 4);
        row.cooldownUntil = sqlite3_column_int64(statement, 5);
        result[groupId] = row;
    }
    sqlite3_finalize(statement);
    return result;
}

void GroupContextStore::saveEngagementCold(std::uint64_t groupId,
                                           const EngagementColdRow &row)
{
    if (db == nullptr)
        return;
    std::lock_guard<std::mutex> lock(mutex);
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO engagement_cold(group_id, hourly_ema, armed_ts,"
                           " day_anchor, cold_today, cooldown_until)"
                           " VALUES(?1, ?2, ?3, ?4, ?5, ?6)"
                           " ON CONFLICT(group_id) DO UPDATE SET"
                           " hourly_ema=?2, armed_ts=?3, day_anchor=?4,"
                           " cold_today=?5, cooldown_until=?6;",
                           -1, &statement, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("冷启动状态落库失败（prepare）");
        return;
    }
    sqlite3_bind_int64(statement, 1, static_cast<std::int64_t>(groupId));
    sqlite3_bind_double(statement, 2, row.hourlyEma);
    sqlite3_bind_int64(statement, 3, row.armedTs);
    sqlite3_bind_int64(statement, 4, row.dayAnchor);
    sqlite3_bind_int(statement, 5, row.coldToday);
    sqlite3_bind_int64(statement, 6, row.cooldownUntil);
    if (sqlite3_step(statement) != SQLITE_DONE)
        LOG_ERROR("冷启动状态落库失败（step）");
    sqlite3_finalize(statement);
}
