#include "PerceptionStore.h"
#include "../Log/Log.h"
#include "../utils/FsUtil.h"

#include <sqlite3.h>

namespace
{
// 旧版本以加盐哈希伪名列 speaker_id 存 speaker（T7b 的伪名化）——哈希不可逆、
// 旧行无法还原为 QQ，且该结构从未发布（用户定规 2026-09-15 改存原始 QQ），
// 检测到即整表重建（观察计数是可再生数据，成本可忽略）
bool hasLegacySpeakerColumn(sqlite3 *db, const char *table)
{
    sqlite3_stmt *statement = nullptr;
    const std::string pragma = std::string("PRAGMA table_info(") + table + ");";
    if (sqlite3_prepare_v2(db, pragma.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        return false;
    bool legacy = false;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const unsigned char *name = sqlite3_column_text(statement, 1);
        if (name != nullptr && std::string(reinterpret_cast<const char *>(name)) == "speaker_id")
            legacy = true;
    }
    sqlite3_finalize(statement);
    return legacy;
}
}

PerceptionStore::PerceptionStore(const std::string &dbPath)
{
    utils::ensureParentDirectories(dbPath);
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("观察通道 SQLite 打开失败，亲密度将仅存内存：" +
                  std::string(db ? sqlite3_errmsg(db) : "?"));
        if (db != nullptr)
            sqlite3_close(db);
        db = nullptr;
        return;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    if (hasLegacySpeakerColumn(db, "affinity"))
    {
        LOG_WARNING("affinity 旧伪名化结构已废弃（改存原始 QQ），整表重建");
        sqlite3_exec(db, "DROP TABLE IF EXISTS affinity;", nullptr, nullptr, nullptr);
    }

    const char *ddl =
        "CREATE TABLE IF NOT EXISTS affinity ("
        " user_id INTEGER NOT NULL,"
        " group_id INTEGER NOT NULL,"
        " observed_count INTEGER NOT NULL DEFAULT 0,"
        " interaction_count INTEGER NOT NULL DEFAULT 0,"
        " last_seen_ts INTEGER NOT NULL,"
        " PRIMARY KEY (user_id, group_id));";
    char *err = nullptr;
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &err) != SQLITE_OK)
    {
        LOG_ERROR("观察通道建表失败，亲密度将仅存内存：" + std::string(err ? err : "?"));
        sqlite3_free(err);
        sqlite3_close(db);
        db = nullptr;
    }
}

PerceptionStore::~PerceptionStore()
{
    if (db != nullptr)
        sqlite3_close(db);
}

void PerceptionStore::upsertBatch(const std::vector<AffinityDelta> &deltas)
{
    if (db == nullptr || deltas.empty())
        return;

    if (sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("观察通道写事务开启失败：" + std::string(sqlite3_errmsg(db)));
        return;
    }

    const char *sql =
        "INSERT INTO affinity (user_id, group_id, observed_count, interaction_count, last_seen_ts)"
        " VALUES (?1, ?2, ?3, ?4, ?5)"
        " ON CONFLICT(user_id, group_id) DO UPDATE SET"
        " observed_count = observed_count + excluded.observed_count,"
        " interaction_count = interaction_count + excluded.interaction_count,"
        " last_seen_ts = excluded.last_seen_ts;";
    sqlite3_stmt *statement = nullptr;
    bool failed = false;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK)
    {
        for (const AffinityDelta &delta : deltas)
        {
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
            sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(delta.userId));
            sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(delta.groupId));
            sqlite3_bind_int64(statement, 3, delta.observedDelta);
            sqlite3_bind_int64(statement, 4, delta.interactionDelta);
            sqlite3_bind_int64(statement, 5, delta.lastSeenTs);
            if (sqlite3_step(statement) != SQLITE_DONE)
            {
                LOG_ERROR("观察通道亲密度写入失败：" + std::string(sqlite3_errmsg(db)));
                failed = true;
                break;
            }
        }
        sqlite3_finalize(statement);
    }
    else
    {
        LOG_ERROR("观察通道写语句准备失败：" + std::string(sqlite3_errmsg(db)));
        failed = true;
    }

    sqlite3_exec(db, failed ? "ROLLBACK;" : "COMMIT;", nullptr, nullptr, nullptr);
}
