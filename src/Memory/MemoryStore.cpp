#include "MemoryStore.h"
#include "../Log/Log.h"
#include <algorithm>
#include <ctime>
#include <unordered_map>

namespace
{
std::string escapeLikePattern(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char character : value)
    {
        if (character == '%' || character == '_' || character == '\\')
            escaped.push_back('\\');
        escaped.push_back(character);
    }
    return escaped;
}
}

MemoryStore::MemoryStore(const std::string &dbPath)
{
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("长期记忆数据库打开失败：" + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        db = nullptr;
        return;
    }

    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    const char *ddl =
        "CREATE TABLE IF NOT EXISTS memories ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " user_id INTEGER NOT NULL,"
        " memory_key TEXT NOT NULL,"
        " memory_type TEXT NOT NULL,"
        " canonical_text TEXT NOT NULL,"
        " search_text TEXT NOT NULL,"
        " importance REAL NOT NULL DEFAULT 0.5,"
        " confidence REAL NOT NULL DEFAULT 0.5,"
        " source_start_id INTEGER NOT NULL DEFAULT 0,"
        " source_end_id INTEGER NOT NULL DEFAULT 0,"
        " active INTEGER NOT NULL DEFAULT 1,"
        " created_at INTEGER NOT NULL,"
        " updated_at INTEGER NOT NULL,"
        " UNIQUE(user_id, memory_key));";
    char *error = nullptr;
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &error) != SQLITE_OK)
    {
        LOG_ERROR("长期记忆建表失败：" + std::string(error ? error : "?"));
        sqlite3_free(error);
        sqlite3_close(db);
        db = nullptr;
        return;
    }

    sqlite3_exec(db,
                 "CREATE INDEX IF NOT EXISTS idx_memories_user_active "
                 "ON memories(user_id, active, updated_at);",
                 nullptr, nullptr, nullptr);
}

MemoryStore::~MemoryStore()
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db != nullptr)
        sqlite3_close(db);
}

bool MemoryStore::isOpen() const
{
    std::lock_guard<std::mutex> lock(dbMutex);
    return db != nullptr;
}

void MemoryStore::upsert(const MemoryItem &item)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db == nullptr || item.memory_key.empty() || item.canonical_text.empty())
        return;

    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    const char *sql =
        "INSERT INTO memories (user_id, memory_key, memory_type, canonical_text, search_text,"
        " importance, confidence, source_start_id, source_end_id, active, created_at, updated_at)"
        " VALUES (?,?,?,?,?,?,?,?,?,1,?,?)"
        " ON CONFLICT(user_id, memory_key) DO UPDATE SET"
        " memory_type=excluded.memory_type, canonical_text=excluded.canonical_text,"
        " search_text=excluded.search_text, importance=excluded.importance,"
        " confidence=excluded.confidence, source_start_id=excluded.source_start_id,"
        " source_end_id=excluded.source_end_id, active=1, updated_at=excluded.updated_at;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("长期记忆 upsert prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(item.user_id));
    sqlite3_bind_text(stmt, 2, item.memory_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, item.memory_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, item.canonical_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, item.search_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, item.importance);
    sqlite3_bind_double(stmt, 7, item.confidence);
    sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(item.source_start_id));
    sqlite3_bind_int64(stmt, 9, static_cast<sqlite3_int64>(item.source_end_id));
    sqlite3_bind_int64(stmt, 10, static_cast<sqlite3_int64>(now));
    sqlite3_bind_int64(stmt, 11, static_cast<sqlite3_int64>(now));
    if (sqlite3_step(stmt) != SQLITE_DONE)
        LOG_ERROR("长期记忆 upsert 失败：" + std::string(sqlite3_errmsg(db)));
    sqlite3_finalize(stmt);
}

void MemoryStore::deactivate(uint64_t user_id, const std::string &memoryKey)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db == nullptr || memoryKey.empty())
        return;
    const char *sql =
        "UPDATE memories SET active=0, updated_at=? WHERE user_id=? AND memory_key=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return;
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(std::time(nullptr)));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_text(stmt, 3, memoryKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void MemoryStore::deactivateBySourceFrom(uint64_t user_id, int64_t firstSourceId)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db == nullptr || firstSourceId <= 0)
        return;
    const char *sql =
        "UPDATE memories SET active=0, updated_at=? "
        "WHERE user_id=? AND source_end_id>=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return;
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(std::time(nullptr)));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(firstSourceId));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void MemoryStore::clearUser(uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db == nullptr)
        return;
    const char *sql = "DELETE FROM memories WHERE user_id=?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return;
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<MemoryItem> MemoryStore::search(uint64_t user_id,
                                            const std::vector<std::string> &queries,
                                            std::size_t limit)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    std::unordered_map<int64_t, MemoryItem> uniqueItems;
    if (db == nullptr || queries.empty() || limit == 0)
        return {};

    const char *sql =
        "SELECT id, memory_key, memory_type, canonical_text, search_text, importance, confidence,"
        " source_start_id, source_end_id, created_at, updated_at "
        "FROM memories WHERE user_id=? AND active=1 "
        "AND (canonical_text LIKE ? ESCAPE '\\' OR search_text LIKE ? ESCAPE '\\') "
        "ORDER BY importance DESC, updated_at DESC LIMIT ?;";

    for (const auto &query : queries)
    {
        if (query.empty())
            continue;
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("长期记忆 search prepare 失败：" + std::string(sqlite3_errmsg(db)));
            break;
        }
        const std::string pattern = "%" + escapeLikePattern(query) + "%";
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, static_cast<int>(limit));
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            MemoryItem item;
            item.id = static_cast<int64_t>(sqlite3_column_int64(stmt, 0));
            item.user_id = user_id;
            item.memory_key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            item.memory_type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
            item.canonical_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
            item.search_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
            item.importance = sqlite3_column_double(stmt, 5);
            item.confidence = sqlite3_column_double(stmt, 6);
            item.source_start_id = static_cast<int64_t>(sqlite3_column_int64(stmt, 7));
            item.source_end_id = static_cast<int64_t>(sqlite3_column_int64(stmt, 8));
            item.created_at = static_cast<int64_t>(sqlite3_column_int64(stmt, 9));
            item.updated_at = static_cast<int64_t>(sqlite3_column_int64(stmt, 10));
            uniqueItems[item.id] = std::move(item);
        }
        sqlite3_finalize(stmt);
    }

    std::vector<MemoryItem> result;
    result.reserve(uniqueItems.size());
    for (auto &entry : uniqueItems)
        result.push_back(std::move(entry.second));
    std::sort(result.begin(), result.end(), [](const MemoryItem &left, const MemoryItem &right) {
        if (left.importance != right.importance)
            return left.importance > right.importance;
        return left.updated_at > right.updated_at;
    });
    if (result.size() > limit)
        result.resize(limit);
    return result;
}
