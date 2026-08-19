#include "ConversationStore.h"
#include "../Log/Log.h"
#include "../Memory/TextRecall.h"
#include <algorithm>
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

ConversationStore::ConversationStore(const std::string &dbPath)
{
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("SQLite 打开失败，对话将仅存内存：" + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        db = nullptr;
        return;
    }
    sqlite3_busy_timeout(db, 5000);
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
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db)
        sqlite3_close(db);
}

int64_t ConversationStore::append(uint64_t user_id, const std::string &role,
                                  const std::string &content, time_t timestamp)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db)
        return 0; // 降级：纯内存模式
    const char *sql =
        "INSERT INTO conversations (user_id, role, content, timestamp) VALUES (?,?,?,?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("append prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return 0;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_text(stmt, 2, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(timestamp));
    int64_t insertedId = 0;
    if (sqlite3_step(stmt) != SQLITE_DONE)
        LOG_ERROR("append step 失败：" + std::string(sqlite3_errmsg(db)));
    else
        insertedId = static_cast<int64_t>(sqlite3_last_insert_rowid(db));
    sqlite3_finalize(stmt);
    return insertedId;
}

std::vector<TimestampedMessage> ConversationStore::loadAll(uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<TimestampedMessage> out;
    if (!db)
        return out;
    const char *sql =
        "SELECT id, role, content, timestamp FROM conversations "
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
        m.id = static_cast<int64_t>(sqlite3_column_int64(stmt, 0));
        m.role = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        m.content = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        m.timestamp = static_cast<time_t>(sqlite3_column_int64(stmt, 3));
        out.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<TimestampedMessage> ConversationStore::search(uint64_t user_id, const std::string &keyword)
{
    auto result = searchMany(user_id, {keyword}, 30);
    std::sort(result.begin(), result.end(), [](const TimestampedMessage &left,
                                               const TimestampedMessage &right) {
        return left.id < right.id;
    });
    return result;
}

std::vector<TimestampedMessage> ConversationStore::searchMany(
    uint64_t user_id, const std::vector<std::string> &queries, std::size_t limit,
    int64_t excludeMessageId)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db || queries.empty() || limit == 0)
        return {};

    const RecallQueryPlan plan = buildRecallQueryPlan(queries);
    if (plan.terms.empty())
        return {};

    std::unordered_map<int64_t, TimestampedMessage> candidates;
    const int candidateLimit = static_cast<int>(std::max<std::size_t>(30, limit * 8));
    const char *sql =
        "SELECT id, role, content, timestamp FROM conversations "
        "WHERE user_id=? AND (?=0 OR id<>?) AND content LIKE ? ESCAPE '\\' "
        "ORDER BY id DESC LIMIT ?;";
    for (const auto &term : plan.terms)
    {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("searchMany prepare 失败：" + std::string(sqlite3_errmsg(db)));
            break;
        }
        const std::string pattern = "%" + escapeLikePattern(term.text) + "%";
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(excludeMessageId));
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(excludeMessageId));
        sqlite3_bind_text(stmt, 4, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, candidateLimit);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            TimestampedMessage message;
            message.id = static_cast<int64_t>(sqlite3_column_int64(stmt, 0));
            message.role = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            message.content = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
            message.timestamp = static_cast<time_t>(sqlite3_column_int64(stmt, 3));
            message.relevance = scoreRecallText(plan, message.content);
            auto iterator = candidates.find(message.id);
            if (iterator == candidates.end() || iterator->second.relevance < message.relevance)
                candidates[message.id] = std::move(message);
        }
        sqlite3_finalize(stmt);
    }

    std::vector<TimestampedMessage> result;
    result.reserve(candidates.size());
    for (auto &entry : candidates)
        result.push_back(std::move(entry.second));
    std::sort(result.begin(), result.end(), [](const TimestampedMessage &left,
                                               const TimestampedMessage &right) {
        if (left.relevance != right.relevance)
            return left.relevance > right.relevance;
        return left.id > right.id;
    });
    if (result.size() > limit)
        result.resize(limit);
    return result;
}

std::vector<TimestampedMessage> ConversationStore::loadByIdRange(
    uint64_t user_id, int64_t start_id, int64_t end_id, int padding)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<TimestampedMessage> out;
    if (!db || start_id <= 0 || end_id <= 0)
        return out;

    if (start_id > end_id)
        std::swap(start_id, end_id);
    const int64_t lowerBound = std::max<int64_t>(1, start_id - std::max(0, padding));
    const int64_t upperBound = end_id + std::max(0, padding);

    const char *sql =
        "SELECT id, role, content, timestamp FROM conversations "
        "WHERE user_id=? AND id BETWEEN ? AND ? ORDER BY id ASC;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("loadByIdRange prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return out;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(lowerBound));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(upperBound));
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        TimestampedMessage message;
        message.id = static_cast<int64_t>(sqlite3_column_int64(stmt, 0));
        message.role = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        message.content = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        message.timestamp = static_cast<time_t>(sqlite3_column_int64(stmt, 3));
        out.push_back(std::move(message));
    }
    sqlite3_finalize(stmt);
    return out;
}

void ConversationStore::clearUser(uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(dbMutex);
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
int64_t ConversationStore::removeLast(uint64_t user_id, int count)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    if (!db || count <= 0)
        return 0;

    int64_t firstRemovedId = 0;
    const char *findSql =
        "SELECT MIN(id) FROM "
        "(SELECT id FROM conversations WHERE user_id=? ORDER BY id DESC LIMIT ?);";
    sqlite3_stmt *findStmt = nullptr;
    if (sqlite3_prepare_v2(db, findSql, -1, &findStmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(findStmt, 1, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_int(findStmt, 2, count);
        if (sqlite3_step(findStmt) == SQLITE_ROW && sqlite3_column_type(findStmt, 0) != SQLITE_NULL)
            firstRemovedId = static_cast<int64_t>(sqlite3_column_int64(findStmt, 0));
    }
    else
    {
        LOG_ERROR("removeLast find prepare 失败：" + std::string(sqlite3_errmsg(db)));
    }
    sqlite3_finalize(findStmt);

    if (firstRemovedId == 0)
        return 0;
    const char *sql =
        "DELETE FROM conversations WHERE id IN "
        "(SELECT id FROM conversations WHERE user_id=? ORDER BY id DESC LIMIT ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_ERROR("removeLast prepare 失败：" + std::string(sqlite3_errmsg(db)));
        return 0;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_int(stmt, 2, count);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        LOG_ERROR("removeLast step 失败：" + std::string(sqlite3_errmsg(db)));
        firstRemovedId = 0;
    }
    sqlite3_finalize(stmt);
    return firstRemovedId;
}
