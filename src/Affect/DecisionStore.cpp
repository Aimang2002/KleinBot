#include "DecisionStore.h"

#include "../Log/Log.h"
#include "../utils/FsUtil.h"

#include <sqlite3.h>

#include <cmath>

namespace affect
{
namespace
{
void setError(std::string *error, const std::string &message)
{
    if (error != nullptr)
        *error = message;
}

bool bindText(sqlite3_stmt *statement, int index, const std::string &value)
{
    return sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string columnText(sqlite3_stmt *statement, int index)
{
    const unsigned char *text = sqlite3_column_text(statement, index);
    return text != nullptr ? std::string(reinterpret_cast<const char *>(text)) : std::string();
}

std::optional<DecisionRecord> readDecision(sqlite3_stmt *statement)
{
    DecisionRecord record;
    record.decisionId = columnText(statement, 0);
    record.createdAt = sqlite3_column_int64(statement, 1);
    record.source = columnText(statement, 2);
    record.groupId = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 3));
    record.userId = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 4));
    record.triggerMessageId = columnText(statement, 5);
    const std::string planJson = columnText(statement, 6);
    record.policyVersion = columnText(statement, 7);
    record.planVersion = columnText(statement, 8);
    record.stateJson = columnText(statement, 9);
    record.fallbackReason = columnText(statement, 10);

    std::string error;
    try
    {
        const nlohmann::json plan = nlohmann::json::parse(planJson);
        const std::optional<BehaviorPlan> parsed = BehaviorPlan::fromJson(plan, error);
        if (!parsed)
        {
            LOG_WARNING("决策记录计划解析失败：" + error + "（decision_id=" + record.decisionId + "）");
            return std::nullopt;
        }
        record.plan = *parsed;
    }
    catch (const std::exception &exception)
    {
        LOG_WARNING("决策记录计划 JSON 损坏：" + std::string(exception.what()) +
                    "（decision_id=" + record.decisionId + "）");
        return std::nullopt;
    }
    return record;
}
} // namespace

DecisionStore::DecisionStore(const std::string &dbPath)
{
    utils::ensureParentDirectories(dbPath);
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("决策记录 SQLite 打开失败：" +
                  std::string(db ? sqlite3_errmsg(db) : "?"));
        if (db != nullptr)
            sqlite3_close(db);
        db = nullptr;
        return;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char *ddl =
        "CREATE TABLE IF NOT EXISTS affect_decisions ("
        " decision_id TEXT PRIMARY KEY,"
        " created_at INTEGER NOT NULL,"
        " source TEXT NOT NULL,"
        " group_id INTEGER NOT NULL DEFAULT 0,"
        " user_id INTEGER NOT NULL DEFAULT 0,"
        " trigger_message_id TEXT NOT NULL DEFAULT '',"
        " plan_json TEXT NOT NULL,"
        " policy_version TEXT NOT NULL DEFAULT '',"
        " plan_version TEXT NOT NULL DEFAULT '',"
        " state_json TEXT NOT NULL DEFAULT '{}',"
        " fallback_reason TEXT NOT NULL DEFAULT '');"
        "CREATE INDEX IF NOT EXISTS idx_affect_decisions_group_time"
        " ON affect_decisions(group_id, created_at);"
        "CREATE TABLE IF NOT EXISTS affect_executions ("
        " execution_id TEXT PRIMARY KEY,"
        " decision_id TEXT NOT NULL,"
        " created_at INTEGER NOT NULL,"
        " status TEXT NOT NULL,"
        " actual_text TEXT NOT NULL DEFAULT '',"
        " outbound_message_id TEXT NOT NULL DEFAULT '',"
        " deviation_reason TEXT NOT NULL DEFAULT '');"
        "CREATE INDEX IF NOT EXISTS idx_affect_executions_decision"
        " ON affect_executions(decision_id);"
        "CREATE TABLE IF NOT EXISTS affect_feedback ("
        " feedback_id TEXT PRIMARY KEY,"
        " decision_id TEXT NOT NULL,"
        " created_at INTEGER NOT NULL,"
        " source TEXT NOT NULL,"
        " status TEXT NOT NULL,"
        " confidence REAL NOT NULL DEFAULT 0,"
        " evidence_json TEXT NOT NULL DEFAULT '{}');"
        "CREATE INDEX IF NOT EXISTS idx_affect_feedback_decision"
        " ON affect_feedback(decision_id);";
    char *error = nullptr;
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &error) != SQLITE_OK)
    {
        LOG_ERROR("决策记录建表失败：" + std::string(error ? error : "?"));
        sqlite3_free(error);
        sqlite3_close(db);
        db = nullptr;
    }
}

DecisionStore::~DecisionStore()
{
    if (db != nullptr)
        sqlite3_close(db);
}

bool DecisionStore::isOpen() const
{
    return db != nullptr;
}

bool DecisionStore::appendDecision(const DecisionRecord &record, std::string *error)
{
    if (db == nullptr)
    {
        setError(error, "决策存储未打开");
        return false;
    }
    if (record.decisionId.empty() || record.source.empty())
    {
        setError(error, "decision_id/source 不能为空");
        return false;
    }
    std::string planError;
    if (!record.plan.validate(planError))
    {
        setError(error, planError);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex);
    const char *sql =
        "INSERT OR IGNORE INTO affect_decisions"
        " (decision_id, created_at, source, group_id, user_id, trigger_message_id,"
        "  plan_json, policy_version, plan_version, state_json, fallback_reason)"
        " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setError(error, std::string("决策写入准备失败：") + sqlite3_errmsg(db));
        return false;
    }

    const std::string planJson = record.plan.toJson().dump();
    bool ok = bindText(statement, 1, record.decisionId) &&
              sqlite3_bind_int64(statement, 2, record.createdAt) == SQLITE_OK &&
              bindText(statement, 3, record.source) &&
              sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(record.groupId)) == SQLITE_OK &&
              sqlite3_bind_int64(statement, 5, static_cast<sqlite3_int64>(record.userId)) == SQLITE_OK &&
              bindText(statement, 6, record.triggerMessageId) &&
              bindText(statement, 7, planJson) &&
              bindText(statement, 8, record.policyVersion) &&
              bindText(statement, 9, record.planVersion) &&
              bindText(statement, 10, record.stateJson) &&
              bindText(statement, 11, record.fallbackReason);
    if (!ok || sqlite3_step(statement) != SQLITE_DONE)
    {
        setError(error, std::string("决策写入失败：") + sqlite3_errmsg(db));
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    return true;
}

bool DecisionStore::appendExecution(const ExecutionRecord &record, std::string *error)
{
    if (db == nullptr)
    {
        setError(error, "决策存储未打开");
        return false;
    }
    if (record.executionId.empty() || record.decisionId.empty() || record.status.empty())
    {
        setError(error, "execution_id/decision_id/status 不能为空");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex);
    const char *sql =
        "INSERT OR IGNORE INTO affect_executions"
        " (execution_id, decision_id, created_at, status, actual_text,"
        "  outbound_message_id, deviation_reason)"
        " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7);";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setError(error, std::string("执行写入准备失败：") + sqlite3_errmsg(db));
        return false;
    }

    bool ok = bindText(statement, 1, record.executionId) &&
              bindText(statement, 2, record.decisionId) &&
              sqlite3_bind_int64(statement, 3, record.createdAt) == SQLITE_OK &&
              bindText(statement, 4, record.status) &&
              bindText(statement, 5, record.actualText) &&
              bindText(statement, 6, record.outboundMessageId) &&
              bindText(statement, 7, record.deviationReason);
    if (!ok || sqlite3_step(statement) != SQLITE_DONE)
    {
        setError(error, std::string("执行写入失败：") + sqlite3_errmsg(db));
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    return true;
}

bool DecisionStore::appendFeedback(const FeedbackRecord &record, std::string *error)
{
    if (db == nullptr)
    {
        setError(error, "决策存储未打开");
        return false;
    }
    if (record.feedbackId.empty() || record.decisionId.empty() || record.source.empty() ||
        record.status.empty())
    {
        setError(error, "feedback_id/decision_id/source/status 不能为空");
        return false;
    }
    if (!std::isfinite(record.confidence) || record.confidence < 0.0 ||
        record.confidence > 1.0)
    {
        setError(error, "feedback confidence 必须是 [0,1] 内的有限值");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex);
    const char *sql =
        "INSERT OR IGNORE INTO affect_feedback"
        " (feedback_id, decision_id, created_at, source, status, confidence, evidence_json)"
        " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7);";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setError(error, std::string("反馈写入准备失败：") + sqlite3_errmsg(db));
        return false;
    }

    bool ok = bindText(statement, 1, record.feedbackId) &&
              bindText(statement, 2, record.decisionId) &&
              sqlite3_bind_int64(statement, 3, record.createdAt) == SQLITE_OK &&
              bindText(statement, 4, record.source) &&
              bindText(statement, 5, record.status) &&
              sqlite3_bind_double(statement, 6, record.confidence) == SQLITE_OK &&
              bindText(statement, 7, record.evidenceJson);
    if (!ok || sqlite3_step(statement) != SQLITE_DONE)
    {
        setError(error, std::string("反馈写入失败：") + sqlite3_errmsg(db));
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    return true;
}

bool DecisionStore::updateExecutionReceipt(const std::string &executionId,
                                           const std::string &status,
                                           const std::string &outboundMessageId,
                                           std::string *error)
{
    if (db == nullptr)
    {
        setError(error, "决策存储未打开");
        return false;
    }
    if (executionId.empty())
    {
        setError(error, "execution_id 不能为空");
        return false;
    }
    if (status != "sent" && status != "failed" && status != "unknown")
    {
        setError(error, "回执状态只能是 sent/failed/unknown：" + status);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex);
    // unknown 只推进 queued；sent/failed 推进 queued 与 unknown。
    // message_id 只在非空时写入：首段 id 放列表最前，已有的分段 id（极少数乱序到达）保留在后
    const char *sql = status == "unknown"
        ? "UPDATE affect_executions SET status=?2,"
          " outbound_message_id=CASE WHEN ?3='' THEN outbound_message_id"
          "   WHEN outbound_message_id='' THEN ?3 ELSE ?3||','||outbound_message_id END"
          " WHERE execution_id=?1 AND status='queued';"
        : "UPDATE affect_executions SET status=?2,"
          " outbound_message_id=CASE WHEN ?3='' THEN outbound_message_id"
          "   WHEN outbound_message_id='' THEN ?3 ELSE ?3||','||outbound_message_id END"
          " WHERE execution_id=?1 AND status IN ('queued','unknown');";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        setError(error, std::string("回执更新准备失败：") + sqlite3_errmsg(db));
        return false;
    }
    const bool ok = bindText(statement, 1, executionId) &&
                    bindText(statement, 2, status) &&
                    bindText(statement, 3, outboundMessageId);
    if (!ok || sqlite3_step(statement) != SQLITE_DONE)
    {
        setError(error, std::string("回执更新失败：") + sqlite3_errmsg(db));
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    if (sqlite3_changes(db) == 0)
    {
        setError(error, "没有可推进的执行记录（不存在或状态已定）：" + executionId);
        return false;
    }
    return true;
}

namespace
{
ExecutionRecord readExecution(sqlite3_stmt *statement)
{
    ExecutionRecord record;
    record.executionId = columnText(statement, 0);
    record.decisionId = columnText(statement, 1);
    record.createdAt = sqlite3_column_int64(statement, 2);
    record.status = columnText(statement, 3);
    record.actualText = columnText(statement, 4);
    record.outboundMessageId = columnText(statement, 5);
    record.deviationReason = columnText(statement, 6);
    return record;
}
} // namespace

std::optional<ExecutionRecord> DecisionStore::findExecution(const std::string &executionId) const
{
    if (db == nullptr || executionId.empty())
        return std::nullopt;

    std::lock_guard<std::mutex> lock(mutex);
    const char *sql =
        "SELECT execution_id, decision_id, created_at, status, actual_text,"
        " outbound_message_id, deviation_reason"
        " FROM affect_executions WHERE execution_id=?1 LIMIT 1;";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_text(statement, 1, executionId.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<ExecutionRecord> result;
    if (sqlite3_step(statement) == SQLITE_ROW)
        result = readExecution(statement);
    sqlite3_finalize(statement);
    return result;
}

bool DecisionStore::appendExecutionMessageId(const std::string &executionId,
                                             const std::string &outboundMessageId,
                                             std::string *error)
{
    if (db == nullptr)
    {
        setError(error, "决策存储未打开");
        return false;
    }
    if (executionId.empty() || outboundMessageId.empty())
    {
        setError(error, "execution_id/message_id 不能为空");
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "UPDATE affect_executions SET outbound_message_id="
                           " CASE WHEN outbound_message_id='' THEN ?2"
                           "      WHEN instr(','||outbound_message_id||',', ','||?2||',')>0"
                           "        THEN outbound_message_id"
                           "      ELSE outbound_message_id||','||?2 END"
                           " WHERE execution_id=?1;",
                           -1, &statement, nullptr) != SQLITE_OK)
    {
        setError(error, std::string("分段 message_id 追加准备失败：") + sqlite3_errmsg(db));
        return false;
    }
    const bool ok = bindText(statement, 1, executionId) && bindText(statement, 2, outboundMessageId);
    if (!ok || sqlite3_step(statement) != SQLITE_DONE)
    {
        setError(error, std::string("分段 message_id 追加失败：") + sqlite3_errmsg(db));
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    if (sqlite3_changes(db) == 0)
    {
        setError(error, "没有该执行记录：" + executionId);
        return false;
    }
    return true;
}

std::optional<ExecutionRecord> DecisionStore::findExecutionByOutboundMessageId(
    const std::string &outboundMessageId) const
{
    if (db == nullptr || outboundMessageId.empty())
        return std::nullopt;

    std::lock_guard<std::mutex> lock(mutex);
    // outbound_message_id 可能是逗号列表（分段投递），任一段命中即可
    const char *sql =
        "SELECT execution_id, decision_id, created_at, status, actual_text,"
        " outbound_message_id, deviation_reason"
        " FROM affect_executions"
        " WHERE instr(','||outbound_message_id||',', ','||?1||',')>0"
        " ORDER BY created_at DESC, execution_id DESC LIMIT 1;";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_text(statement, 1, outboundMessageId.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<ExecutionRecord> result;
    if (sqlite3_step(statement) == SQLITE_ROW)
        result = readExecution(statement);
    sqlite3_finalize(statement);
    return result;
}

std::optional<ExecutionRecord> DecisionStore::latestExecution(std::uint64_t groupId,
                                                              std::uint64_t userId,
                                                              std::int64_t since) const
{
    if (db == nullptr)
        return std::nullopt;

    std::lock_guard<std::mutex> lock(mutex);
    // 群：按群匹配（@ 回复与主动轮的 user_id 不同，只看群）；私聊：group_id=0 且本人
    const char *sql =
        "SELECT e.execution_id, e.decision_id, e.created_at, e.status, e.actual_text,"
        " e.outbound_message_id, e.deviation_reason"
        " FROM affect_executions e JOIN affect_decisions d ON d.decision_id=e.decision_id"
        " WHERE e.created_at>=?3 AND e.status<>'skipped'"
        " AND ((?1<>0 AND d.group_id=?1) OR (?1=0 AND d.group_id=0 AND d.user_id=?2))"
        " ORDER BY e.created_at DESC, e.execution_id DESC LIMIT 1;";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(groupId));
    sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(userId));
    sqlite3_bind_int64(statement, 3, since);
    std::optional<ExecutionRecord> result;
    if (sqlite3_step(statement) == SQLITE_ROW)
        result = readExecution(statement);
    sqlite3_finalize(statement);
    return result;
}

std::optional<DecisionRecord> DecisionStore::findDecision(const std::string &decisionId) const
{
    if (db == nullptr || decisionId.empty())
        return std::nullopt;

    std::lock_guard<std::mutex> lock(mutex);
    const char *sql =
        "SELECT decision_id, created_at, source, group_id, user_id, trigger_message_id,"
        " plan_json, policy_version, plan_version, state_json, fallback_reason"
        " FROM affect_decisions WHERE decision_id=?1 LIMIT 1;";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_text(statement, 1, decisionId.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<DecisionRecord> result;
    if (sqlite3_step(statement) == SQLITE_ROW)
        result = readDecision(statement);
    sqlite3_finalize(statement);
    return result;
}

std::vector<DecisionRecord> DecisionStore::recentDecisions(std::size_t limit) const
{
    std::vector<DecisionRecord> result;
    if (db == nullptr || limit == 0)
        return result;

    std::lock_guard<std::mutex> lock(mutex);
    const char *sql =
        "SELECT decision_id, created_at, source, group_id, user_id, trigger_message_id,"
        " plan_json, policy_version, plan_version, state_json, fallback_reason"
        " FROM affect_decisions ORDER BY created_at DESC, decision_id DESC LIMIT ?1;";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return result;
    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(limit));
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        if (std::optional<DecisionRecord> record = readDecision(statement))
            result.push_back(std::move(*record));
    }
    sqlite3_finalize(statement);
    return result;
}

std::size_t DecisionStore::decisionCount() const
{
    if (db == nullptr)
        return 0;

    std::lock_guard<std::mutex> lock(mutex);
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM affect_decisions;", -1, &statement,
                           nullptr) != SQLITE_OK)
        return 0;
    std::size_t count = 0;
    if (sqlite3_step(statement) == SQLITE_ROW)
        count = static_cast<std::size_t>(sqlite3_column_int64(statement, 0));
    sqlite3_finalize(statement);
    return count;
}

std::size_t DecisionStore::executionCount(const std::string &decisionId) const
{
    if (db == nullptr || decisionId.empty())
        return 0;

    std::lock_guard<std::mutex> lock(mutex);
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "SELECT COUNT(*) FROM affect_executions WHERE decision_id=?1;",
                           -1, &statement, nullptr) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(statement, 1, decisionId.c_str(), -1, SQLITE_TRANSIENT);
    std::size_t count = 0;
    if (sqlite3_step(statement) == SQLITE_ROW)
        count = static_cast<std::size_t>(sqlite3_column_int64(statement, 0));
    sqlite3_finalize(statement);
    return count;
}

std::vector<FeedbackRecord> DecisionStore::feedbackFor(const std::string &decisionId) const
{
    std::vector<FeedbackRecord> result;
    if (db == nullptr || decisionId.empty())
        return result;

    std::lock_guard<std::mutex> lock(mutex);
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "SELECT feedback_id, decision_id, created_at, source, status,"
                           " confidence, evidence_json FROM affect_feedback"
                           " WHERE decision_id=?1 ORDER BY created_at ASC, feedback_id ASC;",
                           -1, &statement, nullptr) != SQLITE_OK)
        return result;
    sqlite3_bind_text(statement, 1, decisionId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        FeedbackRecord record;
        record.feedbackId = columnText(statement, 0);
        record.decisionId = columnText(statement, 1);
        record.createdAt = sqlite3_column_int64(statement, 2);
        record.source = columnText(statement, 3);
        record.status = columnText(statement, 4);
        record.confidence = sqlite3_column_double(statement, 5);
        record.evidenceJson = columnText(statement, 6);
        result.push_back(std::move(record));
    }
    sqlite3_finalize(statement);
    return result;
}

std::size_t DecisionStore::feedbackCount(const std::string &decisionId) const
{
    if (db == nullptr || decisionId.empty())
        return 0;

    std::lock_guard<std::mutex> lock(mutex);
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "SELECT COUNT(*) FROM affect_feedback WHERE decision_id=?1;",
                           -1, &statement, nullptr) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(statement, 1, decisionId.c_str(), -1, SQLITE_TRANSIENT);
    std::size_t count = 0;
    if (sqlite3_step(statement) == SQLITE_ROW)
        count = static_cast<std::size_t>(sqlite3_column_int64(statement, 0));
    sqlite3_finalize(statement);
    return count;
}
} // namespace affect
