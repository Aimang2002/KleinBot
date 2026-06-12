#include "ConversationStore.h"
#include "../Log/Log.h"

ConversationStore::ConversationStore(const std::string &dbPath)
{
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("SQLite 打开失败，对话将仅存内存：" + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        db = nullptr;
        return;
    }
    // WAL 提升并发写稳定性
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char *ddl =
        "CREATE TABLE IF NOT EXISTS conversations ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " user_id INTEGER NOT NULL,"
        " role TEXT NOT NULL,"
        " content TEXT NOT NULL,"
        " timestamp INTEGER NOT NULL);";
    char *err = nullptr;
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &err) != SQLITE_OK)
    {
        LOG_ERROR("SQLite 建表失败，对话将仅存内存：" + std::string(err ? err : "?"));
        sqlite3_free(err);
        sqlite3_close(db);
        db = nullptr;
        return;
    }
    sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS idx_user_time ON conversations(user_id, timestamp);",
        nullptr, nullptr, nullptr);
}

ConversationStore::~ConversationStore()
{
    if (db)
        sqlite3_close(db);
}

void ConversationStore::append(uint64_t user_id, const std::string &role,
                               const std::string &content, time_t timestamp)
{
    if (!db)
        return; // 降级：纯内存模式
    const char *sql =
        "INSERT INTO conversations (user_id, role, content, timestamp) VALUES (?,?,?,?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("append prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_text(stmt, 2, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(timestamp));
    if (sqlite3_step(stmt) != SQLITE_DONE)
        LOG_ERROR("append step 失败：" + std::string(sqlite3_errmsg(db)));
    sqlite3_finalize(stmt);
}

std::vector<TimestampedMessage> ConversationStore::loadAll(uint64_t user_id)
{
    std::vector<TimestampedMessage> out;
    if (!db)
        return out;
    const char *sql =
        "SELECT role, content, timestamp FROM conversations "
        "WHERE user_id=? ORDER BY id ASC;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("loadAll prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return out;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        TimestampedMessage m;
        m.role = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        m.content = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        m.timestamp = static_cast<time_t>(sqlite3_column_int64(stmt, 2));
        out.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<TimestampedMessage> ConversationStore::search(uint64_t user_id, const std::string &keyword)
{
    std::vector<TimestampedMessage> out;
    if (!db)
        return out;
    const char *sql =
        "SELECT role, content, timestamp FROM "
        "(SELECT id, role, content, timestamp FROM conversations "
        " WHERE user_id=? AND content LIKE ? ORDER BY id DESC LIMIT 30) "
        "ORDER BY id ASC;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("search prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return out;
    }
    std::string pattern = "%" + keyword + "%";
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        TimestampedMessage m;
        m.role = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        m.content = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        m.timestamp = static_cast<time_t>(sqlite3_column_int64(stmt, 2));
        out.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return out;
}

void ConversationStore::clearUser(uint64_t user_id)
{
    if (!db)
        return;
    const char *sql = "DELETE FROM conversations WHERE user_id=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("clearUser prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    if (sqlite3_step(stmt) != SQLITE_DONE)
        LOG_ERROR("clearUser step 失败：" + std::string(sqlite3_errmsg(db)));
    sqlite3_finalize(stmt);
}

// 删除该用户末尾 count 条（按 id 倒序选出后删除），与内存删除条数对齐
void ConversationStore::removeLast(uint64_t user_id, int count)
{
    if (!db || count <= 0)
        return;
    const char *sql =
        "DELETE FROM conversations WHERE id IN "
        "(SELECT id FROM conversations WHERE user_id=? ORDER BY id DESC LIMIT ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("removeLast prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_int(stmt, 2, count);
    if (sqlite3_step(stmt) != SQLITE_DONE)
        LOG_ERROR("removeLast step 失败：" + std::string(sqlite3_errmsg(db)));
    sqlite3_finalize(stmt);
}
