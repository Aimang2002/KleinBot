#include "MemoryStore.h"
#include "MemoryQueryPlanner.h"
#include "TextRecall.h"
#include "../Log/Log.h"
#include <algorithm>
#include <ctime>
#include <set>
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

std::string joinAliases(const std::vector<std::string> &aliases)
{
    std::string result;
    for (const auto &alias : aliases)
    {
        if (alias.empty())
            continue;
        if (!result.empty())
            result += " ";
        result += alias;
    }
    return result;
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

    const char *structuredDdl =
        "CREATE TABLE IF NOT EXISTS memory_entities ("
        " user_id INTEGER NOT NULL,"
        " entity_key TEXT NOT NULL,"
        " entity_type TEXT NOT NULL,"
        " canonical_name TEXT NOT NULL,"
        " aliases TEXT NOT NULL DEFAULT '',"
        " created_at INTEGER NOT NULL,"
        " updated_at INTEGER NOT NULL,"
        " PRIMARY KEY(user_id, entity_key));"
        "CREATE TABLE IF NOT EXISTS memory_current_facts ("
        " user_id INTEGER NOT NULL,"
        " memory_key TEXT NOT NULL,"
        " subject_key TEXT NOT NULL,"
        " predicate TEXT NOT NULL,"
        " value_text TEXT NOT NULL,"
        " memory_type TEXT NOT NULL,"
        " canonical_text TEXT NOT NULL,"
        " search_text TEXT NOT NULL,"
        " importance REAL NOT NULL DEFAULT 0.5,"
        " confidence REAL NOT NULL DEFAULT 0.5,"
        " source_start_id INTEGER NOT NULL DEFAULT 0,"
        " source_end_id INTEGER NOT NULL DEFAULT 0,"
        " updated_at INTEGER NOT NULL,"
        " active INTEGER NOT NULL DEFAULT 1,"
        " PRIMARY KEY(user_id, subject_key, predicate));"
        "CREATE TABLE IF NOT EXISTS memory_fact_history ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " user_id INTEGER NOT NULL,"
        " memory_key TEXT NOT NULL,"
        " subject_key TEXT NOT NULL,"
        " predicate TEXT NOT NULL,"
        " value_text TEXT NOT NULL,"
        " memory_type TEXT NOT NULL,"
        " canonical_text TEXT NOT NULL,"
        " search_text TEXT NOT NULL,"
        " importance REAL NOT NULL DEFAULT 0.5,"
        " confidence REAL NOT NULL DEFAULT 0.5,"
        " source_start_id INTEGER NOT NULL DEFAULT 0,"
        " source_end_id INTEGER NOT NULL DEFAULT 0,"
        " valid_from_source_id INTEGER NOT NULL DEFAULT 0,"
        " valid_to_source_id INTEGER,"
        " status TEXT NOT NULL,"
        " created_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_memory_current_fact_lookup "
        "ON memory_current_facts(user_id, subject_key, predicate, active);"
        "CREATE INDEX IF NOT EXISTS idx_memory_fact_history_lookup "
        "ON memory_fact_history(user_id, subject_key, predicate, valid_from_source_id);";
    error = nullptr;
    if (sqlite3_exec(db, structuredDdl, nullptr, nullptr, &error) != SQLITE_OK)
    {
        LOG_ERROR("结构化记忆建表失败：" + std::string(error ? error : "?"));
        sqlite3_free(error);
        sqlite3_close(db);
        db = nullptr;
    }
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
    const bool stored = sqlite3_step(stmt) == SQLITE_DONE;
    if (!stored)
        LOG_ERROR("长期记忆 upsert 失败：" + std::string(sqlite3_errmsg(db)));
    sqlite3_finalize(stmt);
    if (stored)
        upsertStructuredFact(item, now);
}

void MemoryStore::upsertStructuredFact(const MemoryItem &item, int64_t now)
{
    if (item.subject_key.empty() || item.predicate.empty() || item.value_text.empty())
        return;

    std::string subjectType = item.subject_type;
    if (subjectType.empty())
    {
        const std::size_t separator = item.subject_key.find(':');
        subjectType = separator == std::string::npos ? "entity" : item.subject_key.substr(0, separator);
    }
    const std::string subjectName = item.subject_name.empty() ? item.subject_key : item.subject_name;
    const std::string aliases = joinAliases(item.subject_aliases);

    const char *entitySql =
        "INSERT INTO memory_entities "
        "(user_id, entity_key, entity_type, canonical_name, aliases, created_at, updated_at) "
        "VALUES (?,?,?,?,?,?,?) ON CONFLICT(user_id, entity_key) DO UPDATE SET "
        "entity_type=excluded.entity_type, "
        "canonical_name=CASE WHEN excluded.canonical_name=excluded.entity_key "
        "THEN memory_entities.canonical_name ELSE excluded.canonical_name END, "
        "aliases=CASE WHEN excluded.aliases='' THEN memory_entities.aliases ELSE excluded.aliases END, "
        "updated_at=excluded.updated_at;";
    sqlite3_stmt *entityStmt = nullptr;
    if (sqlite3_prepare_v2(db, entitySql, -1, &entityStmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("记忆实体 upsert prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return;
    }
    sqlite3_bind_int64(entityStmt, 1, static_cast<sqlite3_int64>(item.user_id));
    sqlite3_bind_text(entityStmt, 2, item.subject_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(entityStmt, 3, subjectType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(entityStmt, 4, subjectName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(entityStmt, 5, aliases.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(entityStmt, 6, static_cast<sqlite3_int64>(now));
    sqlite3_bind_int64(entityStmt, 7, static_cast<sqlite3_int64>(now));
    if (sqlite3_step(entityStmt) != SQLITE_DONE)
        LOG_ERROR("记忆实体 upsert 失败：" + std::string(sqlite3_errmsg(db)));
    sqlite3_finalize(entityStmt);

    bool currentExists = false;
    bool currentActive = false;
    std::string currentValue;
    const char *currentSql =
        "SELECT value_text, active FROM memory_current_facts "
        "WHERE user_id=? AND subject_key=? AND predicate=?;";
    sqlite3_stmt *currentStmt = nullptr;
    if (sqlite3_prepare_v2(db, currentSql, -1, &currentStmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(currentStmt, 1, static_cast<sqlite3_int64>(item.user_id));
        sqlite3_bind_text(currentStmt, 2, item.subject_key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(currentStmt, 3, item.predicate.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(currentStmt) == SQLITE_ROW)
        {
            currentExists = true;
            currentValue = reinterpret_cast<const char *>(sqlite3_column_text(currentStmt, 0));
            currentActive = sqlite3_column_int(currentStmt, 1) != 0;
        }
    }
    sqlite3_finalize(currentStmt);

    const bool changed = !currentExists || !currentActive || currentValue != item.value_text;
    if (currentExists && currentActive && changed)
    {
        const char *closeSql =
            "UPDATE memory_fact_history SET valid_to_source_id=?, status='superseded' "
            "WHERE user_id=? AND subject_key=? AND predicate=? AND status='active';";
        sqlite3_stmt *closeStmt = nullptr;
        if (sqlite3_prepare_v2(db, closeSql, -1, &closeStmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int64(closeStmt, 1, static_cast<sqlite3_int64>(item.source_start_id));
            sqlite3_bind_int64(closeStmt, 2, static_cast<sqlite3_int64>(item.user_id));
            sqlite3_bind_text(closeStmt, 3, item.subject_key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(closeStmt, 4, item.predicate.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(closeStmt);
        }
        sqlite3_finalize(closeStmt);
    }

    const char *factSql =
        "INSERT INTO memory_current_facts "
        "(user_id, memory_key, subject_key, predicate, value_text, memory_type, canonical_text,"
        " search_text, importance, confidence, source_start_id, source_end_id, updated_at, active)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,1)"
        " ON CONFLICT(user_id, subject_key, predicate) DO UPDATE SET"
        " memory_key=excluded.memory_key, value_text=excluded.value_text,"
        " memory_type=excluded.memory_type, canonical_text=excluded.canonical_text,"
        " search_text=excluded.search_text, importance=excluded.importance,"
        " confidence=excluded.confidence, source_start_id=excluded.source_start_id,"
        " source_end_id=excluded.source_end_id, updated_at=excluded.updated_at, active=1;";
    sqlite3_stmt *factStmt = nullptr;
    if (sqlite3_prepare_v2(db, factSql, -1, &factStmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("当前事实 upsert prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return;
    }
    sqlite3_bind_int64(factStmt, 1, static_cast<sqlite3_int64>(item.user_id));
    sqlite3_bind_text(factStmt, 2, item.memory_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(factStmt, 3, item.subject_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(factStmt, 4, item.predicate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(factStmt, 5, item.value_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(factStmt, 6, item.memory_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(factStmt, 7, item.canonical_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(factStmt, 8, item.search_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(factStmt, 9, item.importance);
    sqlite3_bind_double(factStmt, 10, item.confidence);
    sqlite3_bind_int64(factStmt, 11, static_cast<sqlite3_int64>(item.source_start_id));
    sqlite3_bind_int64(factStmt, 12, static_cast<sqlite3_int64>(item.source_end_id));
    sqlite3_bind_int64(factStmt, 13, static_cast<sqlite3_int64>(now));
    if (sqlite3_step(factStmt) != SQLITE_DONE)
        LOG_ERROR("当前事实 upsert 失败：" + std::string(sqlite3_errmsg(db)));
    sqlite3_finalize(factStmt);

    if (!changed)
        return;

    const char *historySql =
        "INSERT INTO memory_fact_history "
        "(user_id, memory_key, subject_key, predicate, value_text, memory_type, canonical_text,"
        " search_text, importance, confidence, source_start_id, source_end_id,"
        " valid_from_source_id, valid_to_source_id, status, created_at)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,NULL,'active',?);";
    sqlite3_stmt *historyStmt = nullptr;
    if (sqlite3_prepare_v2(db, historySql, -1, &historyStmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("事实历史 insert prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return;
    }
    sqlite3_bind_int64(historyStmt, 1, static_cast<sqlite3_int64>(item.user_id));
    sqlite3_bind_text(historyStmt, 2, item.memory_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(historyStmt, 3, item.subject_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(historyStmt, 4, item.predicate.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(historyStmt, 5, item.value_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(historyStmt, 6, item.memory_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(historyStmt, 7, item.canonical_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(historyStmt, 8, item.search_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(historyStmt, 9, item.importance);
    sqlite3_bind_double(historyStmt, 10, item.confidence);
    sqlite3_bind_int64(historyStmt, 11, static_cast<sqlite3_int64>(item.source_start_id));
    sqlite3_bind_int64(historyStmt, 12, static_cast<sqlite3_int64>(item.source_end_id));
    sqlite3_bind_int64(historyStmt, 13, static_cast<sqlite3_int64>(item.source_start_id));
    sqlite3_bind_int64(historyStmt, 14, static_cast<sqlite3_int64>(now));
    if (sqlite3_step(historyStmt) != SQLITE_DONE)
        LOG_ERROR("事实历史 insert 失败：" + std::string(sqlite3_errmsg(db)));
    sqlite3_finalize(historyStmt);
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
    deactivateStructuredFact(user_id, memoryKey, 0, "retracted");
}

void MemoryStore::deactivateStructuredFact(uint64_t user_id, const std::string &memoryKey,
                                           int64_t validToSourceId,
                                           const std::string &status)
{
    const char *currentSql =
        "UPDATE memory_current_facts SET active=0, updated_at=? "
        "WHERE user_id=? AND memory_key=?;";
    sqlite3_stmt *currentStmt = nullptr;
    if (sqlite3_prepare_v2(db, currentSql, -1, &currentStmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(currentStmt, 1, static_cast<sqlite3_int64>(std::time(nullptr)));
        sqlite3_bind_int64(currentStmt, 2, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_text(currentStmt, 3, memoryKey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(currentStmt);
    }
    sqlite3_finalize(currentStmt);

    const char *historySql =
        "UPDATE memory_fact_history SET status=?, "
        "valid_to_source_id=CASE WHEN ?>0 THEN ? ELSE source_end_id END "
        "WHERE user_id=? AND status!='removed' AND (memory_key=? OR EXISTS ("
        "SELECT 1 FROM memory_current_facts c WHERE c.user_id=memory_fact_history.user_id "
        "AND c.subject_key=memory_fact_history.subject_key "
        "AND c.predicate=memory_fact_history.predicate AND c.memory_key=?));";
    sqlite3_stmt *historyStmt = nullptr;
    if (sqlite3_prepare_v2(db, historySql, -1, &historyStmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(historyStmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(historyStmt, 2, static_cast<sqlite3_int64>(validToSourceId));
        sqlite3_bind_int64(historyStmt, 3, static_cast<sqlite3_int64>(validToSourceId));
        sqlite3_bind_int64(historyStmt, 4, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_text(historyStmt, 5, memoryKey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(historyStmt, 6, memoryKey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(historyStmt);
    }
    sqlite3_finalize(historyStmt);
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
    restoreStructuredFactsBefore(user_id, firstSourceId);
}

void MemoryStore::restoreStructuredFactsBefore(uint64_t user_id, int64_t firstSourceId)
{
    std::vector<std::pair<std::string, std::string>> affected;
    const char *affectedSql =
        "SELECT subject_key, predicate FROM memory_current_facts "
        "WHERE user_id=? AND active=1 AND source_end_id>=?;";
    sqlite3_stmt *affectedStmt = nullptr;
    if (sqlite3_prepare_v2(db, affectedSql, -1, &affectedStmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(affectedStmt, 1, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_int64(affectedStmt, 2, static_cast<sqlite3_int64>(firstSourceId));
        while (sqlite3_step(affectedStmt) == SQLITE_ROW)
        {
            affected.emplace_back(
                reinterpret_cast<const char *>(sqlite3_column_text(affectedStmt, 0)),
                reinterpret_cast<const char *>(sqlite3_column_text(affectedStmt, 1)));
        }
    }
    sqlite3_finalize(affectedStmt);

    for (const auto &key : affected)
    {
        const char *removeSql =
            "UPDATE memory_fact_history SET status='removed', valid_to_source_id=? "
            "WHERE user_id=? AND subject_key=? AND predicate=? AND source_end_id>=?;";
        sqlite3_stmt *removeStmt = nullptr;
        if (sqlite3_prepare_v2(db, removeSql, -1, &removeStmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int64(removeStmt, 1, static_cast<sqlite3_int64>(firstSourceId));
            sqlite3_bind_int64(removeStmt, 2, static_cast<sqlite3_int64>(user_id));
            sqlite3_bind_text(removeStmt, 3, key.first.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(removeStmt, 4, key.second.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(removeStmt, 5, static_cast<sqlite3_int64>(firstSourceId));
            sqlite3_step(removeStmt);
        }
        sqlite3_finalize(removeStmt);

        const char *previousSql =
            "SELECT id, memory_key, value_text, memory_type, canonical_text, search_text,"
            " importance, confidence, source_start_id, source_end_id "
            "FROM memory_fact_history WHERE user_id=? AND subject_key=? AND predicate=? "
            "AND source_end_id<? AND status NOT IN ('removed','retracted') "
            "ORDER BY valid_from_source_id DESC LIMIT 1;";
        sqlite3_stmt *previousStmt = nullptr;
        if (sqlite3_prepare_v2(db, previousSql, -1, &previousStmt, nullptr) != SQLITE_OK)
            continue;
        sqlite3_bind_int64(previousStmt, 1, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_text(previousStmt, 2, key.first.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(previousStmt, 3, key.second.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(previousStmt, 4, static_cast<sqlite3_int64>(firstSourceId));

        if (sqlite3_step(previousStmt) != SQLITE_ROW)
        {
            sqlite3_finalize(previousStmt);
            const char *disableSql =
                "UPDATE memory_current_facts SET active=0, updated_at=? "
                "WHERE user_id=? AND subject_key=? AND predicate=?;";
            sqlite3_stmt *disableStmt = nullptr;
            if (sqlite3_prepare_v2(db, disableSql, -1, &disableStmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int64(disableStmt, 1, static_cast<sqlite3_int64>(std::time(nullptr)));
                sqlite3_bind_int64(disableStmt, 2, static_cast<sqlite3_int64>(user_id));
                sqlite3_bind_text(disableStmt, 3, key.first.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(disableStmt, 4, key.second.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(disableStmt);
            }
            sqlite3_finalize(disableStmt);
            continue;
        }

        const int64_t historyId = static_cast<int64_t>(sqlite3_column_int64(previousStmt, 0));
        const std::string memoryKey = reinterpret_cast<const char *>(sqlite3_column_text(previousStmt, 1));
        const std::string valueText = reinterpret_cast<const char *>(sqlite3_column_text(previousStmt, 2));
        const std::string memoryType = reinterpret_cast<const char *>(sqlite3_column_text(previousStmt, 3));
        const std::string canonicalText = reinterpret_cast<const char *>(sqlite3_column_text(previousStmt, 4));
        const std::string searchText = reinterpret_cast<const char *>(sqlite3_column_text(previousStmt, 5));
        const double importance = sqlite3_column_double(previousStmt, 6);
        const double confidence = sqlite3_column_double(previousStmt, 7);
        const int64_t sourceStartId = static_cast<int64_t>(sqlite3_column_int64(previousStmt, 8));
        const int64_t sourceEndId = static_cast<int64_t>(sqlite3_column_int64(previousStmt, 9));
        sqlite3_finalize(previousStmt);

        const char *restoreSql =
            "UPDATE memory_current_facts SET memory_key=?, value_text=?, memory_type=?,"
            " canonical_text=?, search_text=?, importance=?, confidence=?, source_start_id=?,"
            " source_end_id=?, updated_at=?, active=1 "
            "WHERE user_id=? AND subject_key=? AND predicate=?;";
        sqlite3_stmt *restoreStmt = nullptr;
        if (sqlite3_prepare_v2(db, restoreSql, -1, &restoreStmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(restoreStmt, 1, memoryKey.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(restoreStmt, 2, valueText.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(restoreStmt, 3, memoryType.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(restoreStmt, 4, canonicalText.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(restoreStmt, 5, searchText.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(restoreStmt, 6, importance);
            sqlite3_bind_double(restoreStmt, 7, confidence);
            sqlite3_bind_int64(restoreStmt, 8, static_cast<sqlite3_int64>(sourceStartId));
            sqlite3_bind_int64(restoreStmt, 9, static_cast<sqlite3_int64>(sourceEndId));
            sqlite3_bind_int64(restoreStmt, 10, static_cast<sqlite3_int64>(std::time(nullptr)));
            sqlite3_bind_int64(restoreStmt, 11, static_cast<sqlite3_int64>(user_id));
            sqlite3_bind_text(restoreStmt, 12, key.first.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(restoreStmt, 13, key.second.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(restoreStmt);
        }
        sqlite3_finalize(restoreStmt);

        const char *activateSql =
            "UPDATE memory_fact_history SET status='active', valid_to_source_id=NULL WHERE id=?;";
        sqlite3_stmt *activateStmt = nullptr;
        if (sqlite3_prepare_v2(db, activateSql, -1, &activateStmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int64(activateStmt, 1, static_cast<sqlite3_int64>(historyId));
            sqlite3_step(activateStmt);
        }
        sqlite3_finalize(activateStmt);
    }
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

    for (const char *table : {"memory_current_facts", "memory_fact_history", "memory_entities"})
    {
        const std::string deleteSql = "DELETE FROM " + std::string(table) + " WHERE user_id=?;";
        sqlite3_stmt *deleteStmt = nullptr;
        if (sqlite3_prepare_v2(db, deleteSql.c_str(), -1, &deleteStmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int64(deleteStmt, 1, static_cast<sqlite3_int64>(user_id));
            sqlite3_step(deleteStmt);
        }
        sqlite3_finalize(deleteStmt);
    }
}

std::vector<MemoryItem> MemoryStore::search(uint64_t user_id,
                                            const std::vector<std::string> &queries,
                                            std::size_t limit)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    std::unordered_map<int64_t, MemoryItem> uniqueItems;
    if (db == nullptr || queries.empty() || limit == 0)
        return {};

    const RecallQueryPlan plan = buildRecallQueryPlan(queries);
    if (plan.terms.empty())
        return {};

    const char *sql =
        "SELECT id, memory_key, memory_type, canonical_text, search_text, importance, confidence,"
        " source_start_id, source_end_id, created_at, updated_at "
        "FROM memories WHERE user_id=? AND active=1 "
        "AND (canonical_text LIKE ? ESCAPE '\\' OR search_text LIKE ? ESCAPE '\\') "
        "ORDER BY updated_at DESC LIMIT ?;";

    const int candidateLimit = static_cast<int>(std::max<std::size_t>(32, limit * 8));
    for (const auto &term : plan.terms)
    {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("长期记忆 search prepare 失败：" + std::string(sqlite3_errmsg(db)));
            break;
        }
        const std::string pattern = "%" + escapeLikePattern(term.text) + "%";
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, candidateLimit);
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
            item.relevance = scoreRecallText(
                plan, item.canonical_text + " " + item.search_text) +
                item.importance * 2.0 + item.confidence;
            uniqueItems[item.id] = std::move(item);
        }
        sqlite3_finalize(stmt);
    }

    std::vector<MemoryItem> result;
    result.reserve(uniqueItems.size());
    for (auto &entry : uniqueItems)
        result.push_back(std::move(entry.second));
    std::sort(result.begin(), result.end(), [](const MemoryItem &left, const MemoryItem &right) {
        if (left.relevance != right.relevance)
            return left.relevance > right.relevance;
        return left.updated_at > right.updated_at;
    });
    if (result.size() > limit)
        result.resize(limit);
    return result;
}

std::vector<StructuredFactResult> MemoryStore::searchFacts(
    uint64_t user_id, const std::vector<StructuredFactQuery> &queries, std::size_t limit)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db == nullptr || queries.empty() || limit == 0)
        return {};

    std::vector<StructuredFactResult> results;
    std::set<std::string> uniqueResults;
    for (const auto &query : queries)
    {
        const std::string temporal = query.temporal == "earliest" ||
                                             query.temporal == "previous" ||
                                             query.temporal == "timeline"
                                         ? query.temporal
                                         : "current";
        const RecallQueryPlan subjectPlan = buildRecallQueryPlan({query.subject});
        const RecallQueryPlan predicatePlan = buildRecallQueryPlan({query.predicate});
        auto relevanceFor = [&](const StructuredFactResult &fact) {
            const std::string subjectText = fact.subject_key + " " + fact.subject_type + " " +
                                            fact.subject_name + " " + fact.subject_aliases;
            double relevance = query.subject.empty() ? 10.0 : scoreRecallText(subjectPlan, subjectText);
            if (!query.subject.empty() && relevance <= 0.0)
                return 0.0;
            const std::string predicateText = fact.predicate + " " + fact.canonical_text +
                                              " " + fact.search_text;
            const double predicateRelevance = query.predicate.empty()
                                                  ? 10.0
                                                  : scoreRecallText(predicatePlan, predicateText);
            if (!query.predicate.empty() && predicateRelevance <= 0.0)
                return 0.0;
            if (query.subject == fact.subject_key || query.subject == fact.subject_name)
                relevance += 20.0;
            if (query.predicate == fact.predicate)
                relevance += 20.0;
            return relevance + predicateRelevance + fact.importance * 2.0 + fact.confidence;
        };

        std::vector<StructuredFactResult> candidates;
        if (temporal == "current")
        {
            const char *sql =
                "SELECT f.subject_key, COALESCE(e.entity_type,''),"
                " COALESCE(e.canonical_name,f.subject_key), COALESCE(e.aliases,''),"
                " f.predicate, f.value_text, f.memory_type, f.canonical_text, f.search_text,"
                " f.importance, f.confidence, f.source_start_id, f.source_end_id, f.updated_at "
                "FROM memory_current_facts f LEFT JOIN memory_entities e "
                "ON e.user_id=f.user_id AND e.entity_key=f.subject_key "
                "WHERE f.user_id=? AND f.active=1;";
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
                continue;
            sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                StructuredFactResult fact;
                fact.user_id = user_id;
                fact.subject_key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                fact.subject_type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                fact.subject_name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                fact.subject_aliases = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                fact.predicate = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
                fact.value_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
                fact.memory_type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
                fact.canonical_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
                fact.search_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
                fact.importance = sqlite3_column_double(stmt, 9);
                fact.confidence = sqlite3_column_double(stmt, 10);
                fact.source_start_id = static_cast<int64_t>(sqlite3_column_int64(stmt, 11));
                fact.source_end_id = static_cast<int64_t>(sqlite3_column_int64(stmt, 12));
                fact.valid_from_source_id = fact.source_start_id;
                fact.status = "active";
                fact.temporal = temporal;
                fact.relevance = relevanceFor(fact);
                if (fact.relevance > 0.0)
                    candidates.push_back(std::move(fact));
            }
            sqlite3_finalize(stmt);
        }
        else
        {
            const char *sql =
                "SELECT h.subject_key, COALESCE(e.entity_type,''),"
                " COALESCE(e.canonical_name,h.subject_key), COALESCE(e.aliases,''),"
                " h.predicate, h.value_text, h.memory_type, h.canonical_text, h.search_text,"
                " h.importance, h.confidence, h.source_start_id, h.source_end_id,"
                " h.valid_from_source_id, COALESCE(h.valid_to_source_id,0), h.status "
                "FROM memory_fact_history h LEFT JOIN memory_entities e "
                "ON e.user_id=h.user_id AND e.entity_key=h.subject_key "
                "WHERE h.user_id=? AND h.status NOT IN ('removed','retracted') "
                "ORDER BY h.valid_from_source_id ASC;";
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
                continue;
            sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                StructuredFactResult fact;
                fact.user_id = user_id;
                fact.subject_key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                fact.subject_type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                fact.subject_name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                fact.subject_aliases = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                fact.predicate = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
                fact.value_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
                fact.memory_type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
                fact.canonical_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
                fact.search_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
                fact.importance = sqlite3_column_double(stmt, 9);
                fact.confidence = sqlite3_column_double(stmt, 10);
                fact.source_start_id = static_cast<int64_t>(sqlite3_column_int64(stmt, 11));
                fact.source_end_id = static_cast<int64_t>(sqlite3_column_int64(stmt, 12));
                fact.valid_from_source_id = static_cast<int64_t>(sqlite3_column_int64(stmt, 13));
                fact.valid_to_source_id = static_cast<int64_t>(sqlite3_column_int64(stmt, 14));
                fact.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));
                fact.temporal = temporal;
                fact.relevance = relevanceFor(fact);
                if (fact.relevance > 0.0)
                    candidates.push_back(std::move(fact));
            }
            sqlite3_finalize(stmt);

            std::unordered_map<std::string, std::vector<StructuredFactResult>> grouped;
            for (auto &candidate : candidates)
                grouped[candidate.subject_key + "\n" + candidate.predicate].push_back(std::move(candidate));
            candidates.clear();
            for (auto &entry : grouped)
            {
                auto &versions = entry.second;
                std::sort(versions.begin(), versions.end(), [](const StructuredFactResult &left,
                                                               const StructuredFactResult &right) {
                    return left.valid_from_source_id < right.valid_from_source_id;
                });
                if (temporal == "earliest" && !versions.empty())
                    candidates.push_back(std::move(versions.front()));
                else if (temporal == "previous" && versions.size() >= 2)
                    candidates.push_back(std::move(versions[versions.size() - 2]));
                else if (temporal == "timeline")
                {
                    for (auto &version : versions)
                        candidates.push_back(std::move(version));
                }
            }
        }

        for (auto &candidate : candidates)
        {
            const std::string uniqueKey = candidate.temporal + "\n" + candidate.subject_key + "\n" +
                                          candidate.predicate + "\n" + candidate.value_text + "\n" +
                                          std::to_string(candidate.source_start_id);
            if (uniqueResults.insert(uniqueKey).second)
                results.push_back(std::move(candidate));
        }
    }

    std::sort(results.begin(), results.end(), [](const StructuredFactResult &left,
                                                 const StructuredFactResult &right) {
        if (left.relevance != right.relevance)
            return left.relevance > right.relevance;
        if (left.temporal == "timeline" && right.temporal == "timeline")
            return left.valid_from_source_id < right.valid_from_source_id;
        return left.source_start_id > right.source_start_id;
    });
    if (results.size() > limit)
        results.resize(limit);
    return results;
}

std::vector<StructuredFactQuery> MemoryStore::planFactQueries(
    uint64_t user_id, const std::string &text, std::size_t limit)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db == nullptr || text.empty() || limit == 0)
        return {};

    struct Candidate
    {
        StructuredFactQuery query;
        double score = 0.0;
    };

    const char *sql =
        "SELECT f.subject_key, f.predicate, f.canonical_text, f.search_text,"
        " COALESCE(e.canonical_name,f.subject_key), COALESCE(e.aliases,'') "
        "FROM memory_current_facts f LEFT JOIN memory_entities e "
        "ON e.user_id=f.user_id AND e.entity_key=f.subject_key "
        "WHERE f.user_id=? AND f.active=1;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return {};
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));

    std::vector<Candidate> candidates;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const std::string subjectKey = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const std::string predicate = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        const std::string canonicalText = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        const std::string searchText = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        const std::string subjectName = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        const std::string aliases = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));

        const RecallQueryPlan subjectPlan = buildRecallQueryPlan(
            {subjectKey, subjectName, aliases});
        const RecallQueryPlan predicatePlan = buildRecallQueryPlan(
            {predicate, canonicalText, searchText});
        double subjectScore = scoreRecallText(subjectPlan, text);
        if (subjectKey == "user:self" &&
            (text.find("我") != std::string::npos || text.find("我的") != std::string::npos))
            subjectScore += 20.0;
        if (!subjectName.empty() && text.find(subjectName) != std::string::npos)
            subjectScore += 20.0;
        if (subjectScore <= 0.0)
            continue;

        double predicateScore = scoreRecallText(predicatePlan, text);
        if (text.find(predicate) != std::string::npos)
            predicateScore += 20.0;
        if (predicateScore <= 0.0)
            continue;

        Candidate candidate;
        candidate.query.subject = subjectKey;
        candidate.query.predicate = predicate;
        candidate.query.temporal = detectFactTemporal(text);
        candidate.score = subjectScore + predicateScore;
        candidates.push_back(std::move(candidate));
    }
    sqlite3_finalize(stmt);

    std::sort(candidates.begin(), candidates.end(), [](const Candidate &left,
                                                       const Candidate &right) {
        if (left.score != right.score)
            return left.score > right.score;
        if (left.query.subject != right.query.subject)
            return left.query.subject < right.query.subject;
        return left.query.predicate < right.query.predicate;
    });
    if (candidates.empty())
        return {};

    const double minimumScore = std::max(6.0, candidates.front().score - 6.0);
    std::vector<StructuredFactQuery> result;
    std::set<std::string> uniqueQueries;
    for (const auto &candidate : candidates)
    {
        if (candidate.score < minimumScore || result.size() >= limit)
            break;
        const std::string key = candidate.query.subject + "\n" + candidate.query.predicate +
                                "\n" + candidate.query.temporal;
        if (uniqueQueries.insert(key).second)
            result.push_back(candidate.query);
    }
    return result;
}
