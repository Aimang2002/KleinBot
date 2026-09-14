#include "PerceptionStore.h"
#include "../Log/Log.h"

#include <sqlite3.h>

PerceptionStore::PerceptionStore(const std::string &dbPath)
{
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
