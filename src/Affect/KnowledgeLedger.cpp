#include "KnowledgeLedger.h"

#include "KnowledgeGuard.h"
#include "../Log/Log.h"
#include "../utils/FsUtil.h"
#include "../utils/UniqueId.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <utility>

namespace affect
{
namespace
{
const char *const kEntryColumns =
    "id, kind, scope_group, scope_user, key, text, search_text, strength, status, version,"
    " created_at, updated_at, last_confirmed_at";
const char *const kHistoryColumns =
    "id, knowledge_id, version, text, search_text, strength, status, change, origin,"
    " evidence_json, run_id, created_at";

// 预编译语句的 RAII 外壳
struct Statement
{
    sqlite3_stmt *handle = nullptr;
    Statement(sqlite3 *db, const char *sql)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &handle, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("认识账本 SQL 准备失败：" + std::string(sqlite3_errmsg(db)));
            handle = nullptr;
        }
    }
    ~Statement() { sqlite3_finalize(handle); }
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    explicit operator bool() const { return handle != nullptr; }
    bool text(int index, const std::string &value)
    {
        return sqlite3_bind_text(handle, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
    }
    bool integer(int index, std::int64_t value)
    {
        return sqlite3_bind_int64(handle, index, static_cast<sqlite3_int64>(value)) == SQLITE_OK;
    }
    bool real(int index, double value)
    {
        return sqlite3_bind_double(handle, index, value) == SQLITE_OK;
    }
    std::string columnText(int index) const
    {
        const unsigned char *value = sqlite3_column_text(handle, index);
        return value != nullptr ? std::string(reinterpret_cast<const char *>(value)) : std::string();
    }
};

bool exec(sqlite3 *db, const char *sql)
{
    char *error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK)
    {
        LOG_ERROR("认识账本执行失败：" + std::string(error ? error : "?"));
        sqlite3_free(error);
        return false;
    }
    return true;
}

KnowledgeEntry readEntry(const Statement &statement)
{
    KnowledgeEntry entry;
    entry.id = sqlite3_column_int64(statement.handle, 0);
    entry.kind = statement.columnText(1);
    entry.scopeGroup = static_cast<std::uint64_t>(sqlite3_column_int64(statement.handle, 2));
    entry.scopeUser = static_cast<std::uint64_t>(sqlite3_column_int64(statement.handle, 3));
    entry.key = statement.columnText(4);
    entry.text = statement.columnText(5);
    entry.searchText = statement.columnText(6);
    entry.strength = sqlite3_column_double(statement.handle, 7);
    entry.status = statement.columnText(8);
    entry.version = sqlite3_column_int(statement.handle, 9);
    entry.createdAt = sqlite3_column_int64(statement.handle, 10);
    entry.updatedAt = sqlite3_column_int64(statement.handle, 11);
    entry.lastConfirmedAt = sqlite3_column_int64(statement.handle, 12);
    return entry;
}

KnowledgeHistoryRow readHistory(const Statement &statement)
{
    KnowledgeHistoryRow row;
    row.id = sqlite3_column_int64(statement.handle, 0);
    row.knowledgeId = sqlite3_column_int64(statement.handle, 1);
    row.version = sqlite3_column_int(statement.handle, 2);
    row.text = statement.columnText(3);
    row.searchText = statement.columnText(4);
    row.strength = sqlite3_column_double(statement.handle, 5);
    row.status = statement.columnText(6);
    row.change = statement.columnText(7);
    row.origin = statement.columnText(8);
    row.evidenceJson = statement.columnText(9);
    row.runId = statement.columnText(10);
    row.createdAt = sqlite3_column_int64(statement.handle, 11);
    return row;
}

std::optional<KnowledgeEntry> entryById(sqlite3 *db, std::int64_t id)
{
    const std::string sql = std::string("SELECT ") + kEntryColumns + " FROM affect_knowledge WHERE id=?1;";
    Statement statement(db, sql.c_str());
    if (!statement || !statement.integer(1, id))
        return std::nullopt;
    if (sqlite3_step(statement.handle) != SQLITE_ROW)
        return std::nullopt;
    return readEntry(statement);
}

std::optional<KnowledgeEntry> entryByKey(sqlite3 *db, const std::string &kind, std::uint64_t scopeGroup,
                                         std::uint64_t scopeUser, const std::string &key)
{
    const std::string sql = std::string("SELECT ") + kEntryColumns +
                            " FROM affect_knowledge WHERE kind=?1 AND scope_group=?2 AND"
                            " scope_user=?3 AND key=?4;";
    Statement statement(db, sql.c_str());
    if (!statement || !statement.text(1, kind) ||
        !statement.integer(2, static_cast<std::int64_t>(scopeGroup)) ||
        !statement.integer(3, static_cast<std::int64_t>(scopeUser)) || !statement.text(4, key))
        return std::nullopt;
    if (sqlite3_step(statement.handle) != SQLITE_ROW)
        return std::nullopt;
    return readEntry(statement);
}

std::vector<KnowledgeEntry> entriesWhere(sqlite3 *db, const std::string &where,
                                         const std::function<bool(Statement &)> &bind)
{
    std::vector<KnowledgeEntry> result;
    const std::string sql = std::string("SELECT ") + kEntryColumns + " FROM affect_knowledge" +
                            (where.empty() ? std::string() : " WHERE " + where) +
                            " ORDER BY kind, scope_group, scope_user, id;";
    Statement statement(db, sql.c_str());
    if (!statement || (bind && !bind(statement)))
        return result;
    while (sqlite3_step(statement.handle) == SQLITE_ROW)
        result.push_back(readEntry(statement));
    return result;
}

std::vector<KnowledgeHistoryRow> historyDesc(sqlite3 *db, std::int64_t id)
{
    std::vector<KnowledgeHistoryRow> rows;
    const std::string sql = std::string("SELECT ") + kHistoryColumns +
                            " FROM affect_knowledge_history WHERE knowledge_id=?1 ORDER BY id DESC;";
    Statement statement(db, sql.c_str());
    if (!statement || !statement.integer(1, id))
        return rows;
    while (sqlite3_step(statement.handle) == SQLITE_ROW)
        rows.push_back(readHistory(statement));
    return rows;
}

// 当前起作用的作者（见 KnowledgeLedger::authorOf）；rows 新的在前
std::string authorFrom(const std::vector<KnowledgeHistoryRow> &rows)
{
    for (const KnowledgeHistoryRow &row : rows)
    {
        if (row.origin == "reflection" || row.origin == "teaching")
            return row.origin;
        if (row.origin == "rollback")
        {
            std::string restored;
            try
            {
                const nlohmann::json evidence = nlohmann::json::parse(row.evidenceJson);
                restored = evidence.value("restored_origin", std::string());
            }
            catch (const std::exception &)
            {
            }
            return (restored == "reflection" || restored == "teaching") ? restored
                                                                         : std::string("rollback");
        }
        // decay：遗忘不改作者，往前找
    }
    return {};
}

bool insertEntry(sqlite3 *db, KnowledgeEntry &entry)
{
    Statement statement(db,
                        "INSERT INTO affect_knowledge (kind, scope_group, scope_user, key, text,"
                        " search_text, strength, status, version, created_at, updated_at,"
                        " last_confirmed_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12);");
    const bool ok = statement && statement.text(1, entry.kind) &&
                    statement.integer(2, static_cast<std::int64_t>(entry.scopeGroup)) &&
                    statement.integer(3, static_cast<std::int64_t>(entry.scopeUser)) &&
                    statement.text(4, entry.key) && statement.text(5, entry.text) &&
                    statement.text(6, entry.searchText) && statement.real(7, entry.strength) &&
                    statement.text(8, entry.status) && statement.integer(9, entry.version) &&
                    statement.integer(10, entry.createdAt) && statement.integer(11, entry.updatedAt) &&
                    statement.integer(12, entry.lastConfirmedAt) &&
                    sqlite3_step(statement.handle) == SQLITE_DONE;
    if (ok)
        entry.id = sqlite3_last_insert_rowid(db);
    return ok;
}

bool updateEntry(sqlite3 *db, const KnowledgeEntry &entry)
{
    Statement statement(db,
                        "UPDATE affect_knowledge SET text=?1, search_text=?2, strength=?3, status=?4,"
                        " version=?5, updated_at=?6, last_confirmed_at=?7 WHERE id=?8;");
    return statement && statement.text(1, entry.text) && statement.text(2, entry.searchText) &&
           statement.real(3, entry.strength) && statement.text(4, entry.status) &&
           statement.integer(5, entry.version) && statement.integer(6, entry.updatedAt) &&
           statement.integer(7, entry.lastConfirmedAt) && statement.integer(8, entry.id) &&
           sqlite3_step(statement.handle) == SQLITE_DONE;
}

// 写一个新版本：当前表插入或更新，历史表追加一行（结果状态）
bool writeVersion(sqlite3 *db, KnowledgeEntry &entry, bool isNew, const std::string &change,
                  const std::string &origin, const nlohmann::json &evidence, const std::string &runId,
                  std::int64_t now)
{
    entry.version = isNew ? 1 : entry.version + 1;
    entry.updatedAt = now;
    if (isNew)
        entry.createdAt = now;
    if (!(isNew ? insertEntry(db, entry) : updateEntry(db, entry)))
        return false;
    Statement statement(db,
                        "INSERT INTO affect_knowledge_history (knowledge_id, version, text, search_text,"
                        " strength, status, change, origin, evidence_json, run_id, created_at)"
                        " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);");
    return statement && statement.integer(1, entry.id) && statement.integer(2, entry.version) &&
           statement.text(3, entry.text) && statement.text(4, entry.searchText) &&
           statement.real(5, entry.strength) && statement.text(6, entry.status) &&
           statement.text(7, change) && statement.text(8, origin) &&
           statement.text(9, evidence.dump()) && statement.text(10, runId) &&
           statement.integer(11, now) && sqlite3_step(statement.handle) == SQLITE_DONE;
}

// 规模上限：作用域（kind, scope_group, scope_user）里 active 超过上限时，最弱的先转 faded。
// 教导过的排在最后才轮到；刚写的那一条不动。返回转为 faded 的条数，写失败返回 -1
long enforceScopeLimit(sqlite3 *db, const KnowledgeEntry &written, const std::string &runId,
                       std::int64_t now)
{
    std::vector<KnowledgeEntry> active = entriesWhere(
        db, "kind=?1 AND scope_group=?2 AND scope_user=?3 AND status='active'",
        [&written](Statement &statement)
        {
            return statement.text(1, written.kind) &&
                   statement.integer(2, static_cast<std::int64_t>(written.scopeGroup)) &&
                   statement.integer(3, static_cast<std::int64_t>(written.scopeUser));
        });
    if (active.size() <= KnowledgeRules::kMaxActivePerScope)
        return 0;
    struct Ranked
    {
        bool taught;
        double effective;
        KnowledgeEntry entry;
    };
    std::vector<Ranked> ranked;
    for (KnowledgeEntry &entry : active)
    {
        if (entry.id == written.id)
            continue;
        const std::string author = authorFrom(historyDesc(db, entry.id));
        const bool taught = author == "teaching";
        ranked.push_back({taught, entry.effectiveFor(author, now), std::move(entry)});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b)
              {
                  if (a.taught != b.taught)
                      return !a.taught;
                  if (a.effective != b.effective)
                      return a.effective < b.effective;
                  return a.entry.lastConfirmedAt < b.entry.lastConfirmedAt;
              });
    const std::size_t excess = active.size() - KnowledgeRules::kMaxActivePerScope;
    long faded = 0;
    for (std::size_t index = 0; index < excess && index < ranked.size(); ++index)
    {
        KnowledgeEntry &entry = ranked[index].entry;
        entry.status = "faded";
        if (!writeVersion(db, entry, false, "fade", "decay",
                          nlohmann::json{{"reason", "scope_limit"}}, runId, now))
            return -1;
        ++faded;
    }
    return faded;
}

bool sameState(const KnowledgeEntry &entry, const KnowledgeHistoryRow &row)
{
    return entry.text == row.text && entry.searchText == row.searchText &&
           entry.status == row.status && std::fabs(entry.strength - row.strength) < 1e-9;
}
} // namespace

double effectiveStrength(double strength, std::int64_t lastConfirmedAt, std::int64_t now)
{
    const double days = static_cast<double>(std::max<std::int64_t>(0, now - lastConfirmedAt)) / 86400.0;
    return strength * std::pow(0.5, days / KnowledgeRules::kHalfLifeDays);
}

KnowledgeLedger::KnowledgeLedger(std::string dbPath, std::function<std::int64_t()> clock)
    : path(std::move(dbPath)),
      clock(clock ? std::move(clock)
                  : [] { return static_cast<std::int64_t>(std::time(nullptr)); })
{
}

KnowledgeLedger::~KnowledgeLedger()
{
    if (db != nullptr)
        sqlite3_close(db);
}

bool KnowledgeLedger::open()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db != nullptr)
        return true;
    if (openAttempted)
        return false; // 打开失败过：不反复重试、不反复打日志
    openAttempted = true;

    utils::ensureParentDirectories(path);
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("认识账本 SQLite 打开失败：" + std::string(db ? sqlite3_errmsg(db) : "?"));
        if (db != nullptr)
            sqlite3_close(db);
        db = nullptr;
        return false;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char *ddl =
        "CREATE TABLE IF NOT EXISTS affect_knowledge ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " kind TEXT NOT NULL,"
        " scope_group INTEGER NOT NULL DEFAULT 0,"
        " scope_user INTEGER NOT NULL DEFAULT 0,"
        " key TEXT NOT NULL,"
        " text TEXT NOT NULL,"
        " search_text TEXT NOT NULL DEFAULT '',"
        " strength REAL NOT NULL,"
        " status TEXT NOT NULL,"
        " version INTEGER NOT NULL,"
        " created_at INTEGER NOT NULL,"
        " updated_at INTEGER NOT NULL,"
        " last_confirmed_at INTEGER NOT NULL,"
        " UNIQUE(kind, scope_group, scope_user, key));"
        "CREATE INDEX IF NOT EXISTS idx_affect_knowledge_scope"
        " ON affect_knowledge(scope_group, scope_user, status);"
        "CREATE TABLE IF NOT EXISTS affect_knowledge_history ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " knowledge_id INTEGER NOT NULL,"
        " version INTEGER NOT NULL,"
        " text TEXT NOT NULL,"
        " search_text TEXT NOT NULL DEFAULT '',"
        " strength REAL NOT NULL,"
        " status TEXT NOT NULL,"
        " change TEXT NOT NULL,"
        " origin TEXT NOT NULL,"
        " evidence_json TEXT NOT NULL DEFAULT '{}',"
        " run_id TEXT NOT NULL DEFAULT '',"
        " created_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_affect_knowledge_history_entry"
        " ON affect_knowledge_history(knowledge_id, id);"
        "CREATE INDEX IF NOT EXISTS idx_affect_knowledge_history_run"
        " ON affect_knowledge_history(run_id);";
    if (!exec(db, ddl))
    {
        sqlite3_close(db);
        db = nullptr;
    }
    return db != nullptr;
}

bool KnowledgeLedger::isOpen() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return db != nullptr;
}

KnowledgeRunResult KnowledgeLedger::apply(const std::vector<KnowledgeChange> &changes,
                                          const KnowledgeRunContext &context)
{
    KnowledgeRunResult result;
    result.runId = !context.runId.empty()
                       ? context.runId
                       : utils::uniqueId(context.origin == "teaching" ? "teach" : "run");
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
    {
        result.error = "认识账本未打开";
        return result;
    }
    if (!exec(db, "BEGIN IMMEDIATE;"))
    {
        result.error = "认识账本开事务失败";
        return result;
    }

    const auto reject = [&result](std::size_t index, std::string reason)
    { result.rejected.push_back({index, std::move(reason)}); };
    bool failed = false;
    for (std::size_t index = 0; index < changes.size() && !failed; ++index)
    {
        const KnowledgeChange &change = changes[index];
        if (index >= KnowledgeRules::kMaxChangesPerRun)
        {
            reject(index, "每次至多 8 条改动");
            continue;
        }
        std::string reason;
        if (!KnowledgeGuard::check(change, context, reason))
        {
            reject(index, reason);
            continue;
        }

        std::optional<KnowledgeEntry> existing;
        if (change.targetId != 0)
        {
            existing = entryById(db, change.targetId);
            if (!existing)
            {
                reject(index, "没有这一条");
                continue;
            }
            // 部署者在私聊里按编号改、撤回：哪个范围都行；在群里只能动全局或本群的（不跨群）。
            // 回想按编号改时同样按本次所在的群查
            const bool deployerInPrivate = context.origin == "teaching" && context.groupId == 0;
            if (!deployerInPrivate &&
                !KnowledgeGuard::scopeLegal(existing->kind, existing->scopeGroup,
                                            existing->scopeUser, context, reason))
            {
                reject(index, reason);
                continue;
            }
        }
        else
        {
            existing = entryByKey(db, change.kind, change.scopeGroup, change.scopeUser, change.key);
        }
        // 教导过的认识，回想不能动（设计 §6.2）：只有新的教导或回滚能改它
        if (existing && context.origin == "reflection" &&
            authorFrom(historyDesc(db, existing->id)) == "teaching")
        {
            reject(index, "教导过的认识，回想不能改");
            continue;
        }

        const bool teaching = context.origin == "teaching";
        const std::int64_t at = now();
        nlohmann::json evidence = context.note.is_object() ? context.note : nlohmann::json::object();
        evidence["ids"] = change.evidence;
        if (!change.why.empty())
            evidence["why"] = change.why;

        std::string op = change.op;
        if (op == "add" && existing)
            op = (existing->text == change.text && existing->status == "active") ? "strengthen"
                                                                                  : "modify";
        if (op != "add" && !existing)
        {
            reject(index, "没有这一条");
            continue;
        }
        if ((op == "strengthen" || op == "weaken") && existing->status == "withdrawn")
        {
            reject(index, "这一条已经撤回了");
            continue;
        }
        if (op == "withdraw" && existing->status == "withdrawn")
        {
            reject(index, "这一条已经撤回了");
            continue;
        }

        KnowledgeEntry entry;
        bool isNew = false;
        if (op == "add")
        {
            isNew = true;
            entry.kind = change.kind;
            entry.scopeGroup = change.scopeGroup;
            entry.scopeUser = change.scopeUser;
            entry.key = change.key;
            entry.text = change.text;
            entry.searchText = change.searchText;
            entry.strength = teaching ? KnowledgeRules::kTeachingStrength
                                      : KnowledgeRules::kReflectionStrength;
            entry.status = "active";
            entry.lastConfirmedAt = at;
        }
        else
        {
            entry = *existing;
            if (op == "modify")
            {
                entry.text = change.text;
                if (!change.searchText.empty())
                    entry.searchText = change.searchText;
                entry.strength = teaching ? KnowledgeRules::kTeachingStrength
                                          : std::max(entry.strength, KnowledgeRules::kReflectionStrength);
                entry.status = "active";
                entry.lastConfirmedAt = at;
            }
            else if (op == "strengthen")
            {
                entry.strength = teaching ? KnowledgeRules::kTeachingStrength
                                          : std::min(1.0, entry.strength + KnowledgeRules::kStrengthStep);
                entry.status = "active";
                entry.lastConfirmedAt = at;
            }
            else if (op == "weaken")
            {
                entry.strength = std::max(0.0, entry.strength - KnowledgeRules::kStrengthStep);
                // 写完之后的作者：教导写的就是教导（不衰减），回想写的只会是回想
                if (entry.effectiveFor(context.origin, at) < KnowledgeRules::kFadeThreshold)
                    entry.status = "faded";
            }
            else // withdraw
            {
                entry.status = "withdrawn";
            }
        }

        if (!writeVersion(db, entry, isNew, op, context.origin, evidence, result.runId, at))
        {
            failed = true;
            break;
        }
        if (entry.status == "active")
        {
            const long faded = enforceScopeLimit(db, entry, result.runId, at);
            if (faded < 0)
            {
                failed = true;
                break;
            }
            result.scopeFaded += static_cast<std::size_t>(faded);
        }
        result.written.push_back(entry.id);
        result.applied.push_back(op);
    }

    if (failed || !exec(db, "COMMIT;"))
    {
        exec(db, "ROLLBACK;");
        result.error = std::string("认识账本写入失败：") + sqlite3_errmsg(db);
        result.written.clear();
        result.applied.clear();
        LOG_ERROR("认识账本：运行 " + result.runId + " 写入失败，已整体撤销");
        return result;
    }
    result.ok = true;
    // 日志只记条数、编号与原因，不记正文（设计 §8）
    std::string ids;
    for (std::int64_t id : result.written)
        ids += (ids.empty() ? "#" : "、#") + std::to_string(id);
    LOG_INFO("认识账本：运行 " + result.runId + "（" + context.origin + "）写入 " +
             std::to_string(result.written.size()) + " 条" + (ids.empty() ? "" : "（" + ids + "）") +
             "，丢弃 " + std::to_string(result.rejected.size()) + " 条" +
             (result.scopeFaded > 0 ? "，规模上限转淡 " + std::to_string(result.scopeFaded) + " 条"
                                    : std::string()));
    for (const RejectedChange &rejected : result.rejected)
        LOG_INFO("认识账本：运行 " + result.runId + " 第 " + std::to_string(rejected.index + 1) +
                 " 条丢弃：" + rejected.reason);
    return result;
}

namespace
{
// 回滚的唯一实现（锁内、事务内）：kept 判断某条历史是否在回滚点之前
bool rollbackLocked(sqlite3 *db, const std::function<bool(const KnowledgeHistoryRow &)> &kept,
                    const std::string &label, const std::string &runId, std::int64_t now,
                    std::size_t &changed)
{
    for (KnowledgeEntry entry : entriesWhere(db, std::string(), {}))
    {
        std::vector<KnowledgeHistoryRow> rows;
        for (KnowledgeHistoryRow &row : historyDesc(db, entry.id))
            if (kept(row))
                rows.push_back(std::move(row));
        nlohmann::json evidence{{"to", label}};
        if (rows.empty())
        {
            // 那时还没有这一条：不删，记为撤回
            if (entry.status == "withdrawn")
                continue;
            entry.status = "withdrawn";
            evidence["to_version"] = 0;
            evidence["restored_origin"] = "";
        }
        else
        {
            const KnowledgeHistoryRow &target = rows.front();
            if (sameState(entry, target))
                continue;
            entry.text = target.text;
            entry.searchText = target.searchText;
            entry.strength = target.strength;
            entry.status = target.status;
            // 上次印证 = 回滚点之前最近一次 add / modify / strengthen
            entry.lastConfirmedAt = target.createdAt;
            for (const KnowledgeHistoryRow &row : rows)
                if (row.change == "add" || row.change == "modify" || row.change == "strengthen")
                {
                    entry.lastConfirmedAt = row.createdAt;
                    break;
                }
            evidence["to_version"] = target.version;
            evidence["restored_origin"] = authorFrom(rows);
        }
        if (!writeVersion(db, entry, false, "rollback", "rollback", evidence, runId, now))
            return false;
        ++changed;
    }
    return true;
}
} // namespace

KnowledgeRollbackResult KnowledgeLedger::rollbackTo(std::int64_t at)
{
    KnowledgeRollbackResult result;
    result.runId = utils::uniqueId("rollback");
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
    {
        result.error = "认识账本未打开";
        return result;
    }
    if (!exec(db, "BEGIN IMMEDIATE;"))
        return result;
    const bool ok = rollbackLocked(
        db, [at](const KnowledgeHistoryRow &row) { return row.createdAt <= at; },
        "time:" + std::to_string(at), result.runId, now(), result.changed);
    if (!ok || !exec(db, "COMMIT;"))
    {
        exec(db, "ROLLBACK;");
        result.changed = 0;
        result.error = "认识账本回滚失败";
        LOG_ERROR("认识账本：回滚到 " + std::to_string(at) + " 失败，已整体撤销");
        return result;
    }
    result.ok = true;
    LOG_INFO("认识账本：回滚到 " + std::to_string(at) + "（运行 " + result.runId + "），改动 " +
             std::to_string(result.changed) + " 条");
    return result;
}

KnowledgeRollbackResult KnowledgeLedger::rollbackBefore(const std::string &runId)
{
    KnowledgeRollbackResult result;
    result.runId = utils::uniqueId("rollback");
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
    {
        result.error = "认识账本未打开";
        return result;
    }
    std::int64_t firstRow = 0;
    {
        Statement statement(db, "SELECT MIN(id) FROM affect_knowledge_history WHERE run_id=?1;");
        if (statement && statement.text(1, runId) && sqlite3_step(statement.handle) == SQLITE_ROW &&
            sqlite3_column_type(statement.handle, 0) != SQLITE_NULL)
            firstRow = sqlite3_column_int64(statement.handle, 0);
    }
    if (runId.empty() || firstRow == 0)
    {
        result.ok = true;
        result.runFound = false;
        return result;
    }
    if (!exec(db, "BEGIN IMMEDIATE;"))
        return result;
    const bool ok = rollbackLocked(
        db, [firstRow](const KnowledgeHistoryRow &row) { return row.id < firstRow; },
        "before_run:" + runId, result.runId, now(), result.changed);
    if (!ok || !exec(db, "COMMIT;"))
    {
        exec(db, "ROLLBACK;");
        result.changed = 0;
        result.error = "认识账本回滚失败";
        LOG_ERROR("认识账本：回滚到运行 " + runId + " 之前失败，已整体撤销");
        return result;
    }
    result.ok = true;
    LOG_INFO("认识账本：回滚到运行 " + runId + " 之前（运行 " + result.runId + "），改动 " +
             std::to_string(result.changed) + " 条");
    return result;
}

std::size_t KnowledgeLedger::applyDecay()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return 0;
    const std::int64_t at = now();
    std::vector<KnowledgeEntry> active = entriesWhere(db, "status='active'", {});
    std::vector<KnowledgeEntry> fading;
    for (KnowledgeEntry &entry : active)
    {
        // 教导的认识不衰减（部署者的纠正分量最高），遗忘不碰它
        if (authorFrom(historyDesc(db, entry.id)) == "teaching")
            continue;
        if (entry.effective(at) < KnowledgeRules::kFadeThreshold)
            fading.push_back(std::move(entry));
    }
    if (fading.empty())
        return 0;
    if (!exec(db, "BEGIN IMMEDIATE;"))
        return 0;
    const std::string runId = utils::uniqueId("decay");
    for (KnowledgeEntry &entry : fading)
    {
        const double effective = entry.effective(at);
        entry.status = "faded";
        if (!writeVersion(db, entry, false, "fade", "decay",
                          nlohmann::json{{"reason", "decay"},
                                         {"effective", std::round(effective * 1000.0) / 1000.0}},
                          runId, at))
        {
            exec(db, "ROLLBACK;");
            return 0;
        }
    }
    if (!exec(db, "COMMIT;"))
    {
        exec(db, "ROLLBACK;");
        return 0;
    }
    LOG_INFO("认识账本：遗忘 " + std::to_string(fading.size()) + " 条（运行 " + runId + "）");
    return fading.size();
}

namespace
{
std::size_t forgetWhere(sqlite3 *db, const char *column, std::uint64_t value)
{
    const std::string history = std::string("DELETE FROM affect_knowledge_history WHERE knowledge_id IN"
                                            " (SELECT id FROM affect_knowledge WHERE ") +
                                column + "=?1);";
    const std::string current = std::string("DELETE FROM affect_knowledge WHERE ") + column + "=?1;";
    if (!exec(db, "BEGIN IMMEDIATE;"))
        return 0;
    std::size_t removed = 0;
    bool ok = false;
    {
        Statement first(db, history.c_str());
        ok = first && first.integer(1, static_cast<std::int64_t>(value)) &&
             sqlite3_step(first.handle) == SQLITE_DONE;
    }
    if (ok)
    {
        Statement second(db, current.c_str());
        ok = second && second.integer(1, static_cast<std::int64_t>(value)) &&
             sqlite3_step(second.handle) == SQLITE_DONE;
        if (ok)
            removed = static_cast<std::size_t>(sqlite3_changes(db));
    }
    if (!ok || !exec(db, "COMMIT;"))
    {
        exec(db, "ROLLBACK;");
        return 0;
    }
    return removed;
}
} // namespace

std::size_t KnowledgeLedger::forgetPerson(std::uint64_t userId)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr || userId == 0)
        return 0;
    const std::size_t removed = forgetWhere(db, "scope_user", userId);
    LOG_INFO("认识账本：按人清除 " + std::to_string(removed) + " 条");
    return removed;
}

std::size_t KnowledgeLedger::forgetGroup(std::uint64_t groupId)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr || groupId == 0)
        return 0;
    const std::size_t removed = forgetWhere(db, "scope_group", groupId);
    LOG_INFO("认识账本：按群清除 " + std::to_string(removed) + " 条（群 " + std::to_string(groupId) + "）");
    return removed;
}

std::optional<KnowledgeEntry> KnowledgeLedger::find(std::int64_t id) const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return std::nullopt;
    return entryById(db, id);
}

std::vector<KnowledgeEntry> KnowledgeLedger::query(const KnowledgeQuery &filter) const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return {};
    std::string where = filter.includeWithdrawn ? "1=1" : "status<>'withdrawn'";
    if (filter.kind)
        where += " AND kind=?1";
    if (filter.scopeGroup)
        where += " AND scope_group=?2";
    if (filter.scopeUser)
        where += " AND scope_user=?3";
    return entriesWhere(db, where, [&filter](Statement &statement)
                        {
                            bool ok = true;
                            if (filter.kind)
                                ok = ok && statement.text(1, *filter.kind);
                            if (filter.scopeGroup)
                                ok = ok && statement.integer(2, static_cast<std::int64_t>(*filter.scopeGroup));
                            if (filter.scopeUser)
                                ok = ok && statement.integer(3, static_cast<std::int64_t>(*filter.scopeUser));
                            return ok;
                        });
}

std::vector<KnowledgeHistoryRow> KnowledgeLedger::history(std::int64_t id) const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return {};
    return historyDesc(db, id);
}

std::string KnowledgeLedger::authorOf(std::int64_t id) const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return {};
    return authorFrom(historyDesc(db, id));
}

double KnowledgeLedger::effectiveOf(const KnowledgeEntry &entry) const
{
    std::lock_guard<std::mutex> lock(mutex);
    const std::string author = db != nullptr ? authorFrom(historyDesc(db, entry.id)) : std::string();
    return entry.effectiveFor(author, now());
}

std::vector<KnowledgeEntry> KnowledgeLedger::candidatesFor(std::uint64_t groupId,
                                                           const std::vector<std::uint64_t> &people) const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return {};
    const std::int64_t at = now();
    const std::set<std::uint64_t> present(people.begin(), people.end());
    std::vector<KnowledgeEntry> rows = entriesWhere(
        db, "status='active' AND (scope_group=0 OR scope_group=?1)",
        [groupId](Statement &statement)
        { return statement.integer(1, static_cast<std::int64_t>(groupId)); });
    std::vector<std::pair<double, KnowledgeEntry>> kept;
    for (KnowledgeEntry &entry : rows)
    {
        const bool inScope =
            entry.kind == "habit" ||
            (entry.kind == "group" && groupId != 0 && entry.scopeGroup == groupId) ||
            (entry.kind == "person" && present.count(entry.scopeUser) > 0);
        if (!inScope)
            continue;
        const double effective = entry.effectiveFor(authorFrom(historyDesc(db, entry.id)), at);
        if (effective >= KnowledgeRules::kFadeThreshold)
            kept.emplace_back(effective, std::move(entry));
    }
    std::stable_sort(kept.begin(), kept.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    std::vector<KnowledgeEntry> result;
    for (auto &item : kept)
        result.push_back(std::move(item.second));
    return result;
}

std::vector<RecalledKnowledge> KnowledgeLedger::recallPool(std::uint64_t groupId) const
{
    // 与其它读方法同一把锁：判定在工作线程跑，账本工具在私聊路径上写，连接只在锁内用
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return {};
    const std::int64_t at = now();
    std::vector<KnowledgeEntry> rows = entriesWhere(
        db, "status='active' AND (scope_group=0 OR scope_group=?1)",
        [groupId](Statement &statement)
        { return statement.integer(1, static_cast<std::int64_t>(groupId)); });
    std::vector<RecalledKnowledge> pool;
    pool.reserve(rows.size());
    for (KnowledgeEntry &entry : rows)
    {
        const std::string author = authorFrom(historyDesc(db, entry.id));
        RecalledKnowledge item;
        item.effective = entry.effectiveFor(author, at);
        item.teaching = author == "teaching";
        item.entry = std::move(entry);
        pool.push_back(std::move(item));
    }
    return pool;
}
} // namespace affect
