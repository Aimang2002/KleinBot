#include "ReminderStore.h"
#include "../Log/Log.h"

ReminderStore::ReminderStore(const std::string &dbPath)
{
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("SQLite 打开失败，提醒将不可持久化：" + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        db = nullptr;
        return;
    }
    sqlite3_busy_timeout(db, 5000);
    // WAL 提升并发写稳定性
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char *ddl =
        "CREATE TABLE IF NOT EXISTS reminders ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " user_id INTEGER NOT NULL,"
        " content TEXT NOT NULL,"
        " trigger_at INTEGER NOT NULL,"
        " repeat_rule TEXT NOT NULL DEFAULT 'none',"
        " created_at INTEGER NOT NULL);";
    char *err = nullptr;
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &err) != SQLITE_OK)
    {
        LOG_ERROR("SQLite 建表失败，提醒将不可持久化：" + std::string(err ? err : "?"));
        sqlite3_free(err);
        sqlite3_close(db);
        db = nullptr;
        return;
    }
    sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS idx_reminders_due ON reminders(trigger_at);",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS idx_reminders_user ON reminders(user_id, trigger_at);",
        nullptr, nullptr, nullptr);
}

ReminderStore::~ReminderStore()
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db)
        sqlite3_close(db);
}

ReminderRecord ReminderStore::rowToRecord(sqlite3_stmt *stmt)
{
    ReminderRecord record;
    record.id = static_cast<int64_t>(sqlite3_column_int64(stmt, 0));
    record.user_id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
    record.content = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    record.trigger_at = static_cast<int64_t>(sqlite3_column_int64(stmt, 3));
    const char *repeat = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    record.repeat_rule = repeat ? repeat : "none";
    record.created_at = static_cast<int64_t>(sqlite3_column_int64(stmt, 5));
    return record;
}

int64_t ReminderStore::insert(uint64_t user_id, const std::string &content,
                              int64_t trigger_at, const std::string &repeat_rule)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db)
        return 0;
    const char *sql =
        "INSERT INTO reminders (user_id, content, trigger_at, repeat_rule, created_at) "
        "VALUES (?,?,?,?,?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("reminders insert prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return 0;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(trigger_at));
    sqlite3_bind_text(stmt, 4, repeat_rule.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(std::time(nullptr)));
    int64_t insertedId = 0;
    if (sqlite3_step(stmt) != SQLITE_DONE)
        LOG_ERROR("reminders insert step 失败：" + std::string(sqlite3_errmsg(db)));
    else
        insertedId = static_cast<int64_t>(sqlite3_last_insert_rowid(db));
    sqlite3_finalize(stmt);
    return insertedId;
}

std::vector<ReminderRecord> ReminderStore::listForUser(uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<ReminderRecord> out;
    if (!db)
        return out;
    const char *sql =
        "SELECT id, user_id, content, trigger_at, repeat_rule, created_at "
        "FROM reminders WHERE user_id=? ORDER BY trigger_at ASC, id ASC;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("reminders listForUser prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return out;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        out.push_back(rowToRecord(stmt));
    sqlite3_finalize(stmt);
    return out;
}

std::size_t ReminderStore::countForUser(uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    std::size_t count = 0;
    if (!db)
        return count;
    const char *sql = "SELECT COUNT(*) FROM reminders WHERE user_id=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("reminders countForUser prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return count;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    return count;
}

std::vector<ReminderRecord> ReminderStore::loadPending()
{
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<ReminderRecord> out;
    if (!db)
        return out;
    const char *sql =
        "SELECT id, user_id, content, trigger_at, repeat_rule, created_at "
        "FROM reminders ORDER BY trigger_at ASC, id ASC;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("reminders loadPending prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return out;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW)
        out.push_back(rowToRecord(stmt));
    sqlite3_finalize(stmt);
    return out;
}

bool ReminderStore::deleteById(int64_t id)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db)
        return false;
    const char *sql = "DELETE FROM reminders WHERE id=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("reminders delete prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return false;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(id));
    const bool deleted = sqlite3_step(stmt) == SQLITE_DONE &&
                         sqlite3_changes(db) > 0;
    sqlite3_finalize(stmt);
    return deleted;
}

bool ReminderStore::updateTrigger(int64_t id, int64_t trigger_at)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db)
        return false;
    const char *sql = "UPDATE reminders SET trigger_at=? WHERE id=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("reminders updateTrigger prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return false;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(trigger_at));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(id));
    const bool updated = sqlite3_step(stmt) == SQLITE_DONE &&
                         sqlite3_changes(db) > 0;
    sqlite3_finalize(stmt);
    return updated;
}
