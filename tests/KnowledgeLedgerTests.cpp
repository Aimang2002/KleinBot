#include <gtest/gtest.h>

#include "Action/KnowledgeAction.h"
#include "Affect/BehaviorPlan.h"
#include "Affect/DecisionRecord.h"
#include "Affect/DecisionRecorder.h"
#include "Affect/DecisionStore.h"
#include "Affect/InTheMoment.h"
#include "Affect/KnowledgeGuard.h"
#include "Affect/KnowledgeLedger.h"
#include "Tool/ActionTool.h"
#include "Tool/ToolRegistry.h"
#include "utils/LocalDate.h"

#include <sqlite3.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// 经历层 E2（ADR-028）：认识账本、代码校验、当场察觉、管理员私聊里的账本工具
namespace
{
constexpr std::int64_t kDay = 86400;
constexpr std::uint64_t kGroup = 882926802;
constexpr std::uint64_t kOtherGroup = 123456789;
constexpr std::uint64_t kManager = 10001;
constexpr std::uint64_t kAqing = 20002;
constexpr std::uint64_t kLaoyu = 30003;

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-knowledge-XXXXXX";
        pattern.push_back('\0');
        char *created = mkdtemp(pattern.data());
        if (created != nullptr)
            directory = created;
    }
    ~TemporaryDirectory()
    {
        if (!directory.empty())
            std::filesystem::remove_all(directory);
    }
    const std::string &path() const { return directory; }

private:
    std::string directory;
};

struct LedgerHarness
{
    TemporaryDirectory directory;
    std::int64_t now = 1763600000; // 2025-11-20 前后，具体值无关
    affect::KnowledgeLedger ledger{directory.path() + "/affect.db", [this] { return now; }};

    LedgerHarness() { EXPECT_TRUE(ledger.open()); }

    affect::KnowledgeRunContext teaching(std::uint64_t groupId = 0) const
    {
        affect::KnowledgeRunContext context;
        context.origin = "teaching";
        context.groupId = groupId;
        context.evidenceExists = [](const std::string &) { return true; };
        return context;
    }
    affect::KnowledgeRunContext reflection(std::uint64_t groupId = kGroup) const
    {
        affect::KnowledgeRunContext context;
        context.origin = "reflection";
        context.groupId = groupId;
        context.presentUsers = {kAqing};
        context.evidenceExists = [](const std::string &id) { return id != "decision:missing"; };
        return context;
    }
};

std::string repeated(const std::string &unit, int count)
{
    std::string value;
    for (int i = 0; i < count; ++i)
        value += unit;
    return value;
}

affect::KnowledgeChange change(const std::string &op, const std::string &kind, const std::string &key,
                               const std::string &text, std::uint64_t scopeGroup = 0,
                               std::uint64_t scopeUser = 0)
{
    affect::KnowledgeChange value;
    value.op = op;
    value.kind = kind;
    value.key = key;
    value.text = text;
    value.searchText = text;
    value.scopeGroup = scopeGroup;
    value.scopeUser = scopeUser;
    value.evidence = {"decision:dec_1"};
    return value;
}

std::vector<std::string> columnsOf(const std::string &path, const std::string &table)
{
    std::vector<std::string> columns;
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK)
        return columns;
    sqlite3_stmt *statement = nullptr;
    const std::string sql = "PRAGMA table_info(" + table + ");";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK)
        while (sqlite3_step(statement) == SQLITE_ROW)
            columns.emplace_back(reinterpret_cast<const char *>(sqlite3_column_text(statement, 1)));
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return columns;
}

// 库里所有文本（两张账本表的全部文本列），用来查有没有群友原文
std::string allLedgerText(const std::string &path)
{
    std::string all;
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK)
        return all;
    for (const char *sql : {"SELECT kind || key || text || search_text || status FROM affect_knowledge;",
                            "SELECT text || search_text || change || origin || evidence_json || run_id"
                            " FROM affect_knowledge_history;"})
    {
        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK)
            while (sqlite3_step(statement) == SQLITE_ROW)
                all += reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
        sqlite3_finalize(statement);
    }
    sqlite3_close(db);
    return all;
}

std::size_t historyRowCount(const std::string &path)
{
    sqlite3 *db = nullptr;
    std::size_t count = 0;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK)
        return count;
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM affect_knowledge_history;", -1, &statement,
                           nullptr) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW)
        count = static_cast<std::size_t>(sqlite3_column_int64(statement, 0));
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return count;
}

// 账本工具的一次调用（管理员私聊里的管理员）：只取回灌给模型的那段文字
std::string call(Action &action, const nlohmann::json &arguments)
{
    const ActionResult result = action.execute(arguments, ActionContext{kManager, 77, "原话不进账本"});
    EXPECT_FALSE(result.terminal) << "结果回灌模型，由她用自己的话回复";
    return result.content;
}

const Live<AffectOptions> &affectOn()
{
    static const Live<AffectOptions> live{AffectOptions{true}};
    return live;
}
const Live<AffectOptions> &affectOff()
{
    static const Live<AffectOptions> live{AffectOptions{}};
    return live;
}
} // namespace

// ---------- 表与延迟打开 ----------

TEST(KnowledgeLedgerTest, OpensLazilyAndCreatesBothTablesIdempotently)
{
    TemporaryDirectory directory;
    const std::string path = directory.path() + "/affect.db";
    affect::KnowledgeLedger ledger(path);
    EXPECT_FALSE(ledger.isOpen());
    EXPECT_FALSE(std::filesystem::exists(path)) << "构造时不碰数据库";
    EXPECT_TRUE(ledger.query({}).empty());
    EXPECT_FALSE(ledger.apply({change("add", "habit", "k", "一句")}, {}).ok) << "未打开不写";

    ASSERT_TRUE(ledger.open());
    ASSERT_TRUE(ledger.open()) << "幂等";
    affect::KnowledgeLedger second(path);
    ASSERT_TRUE(second.open()) << "再建一次表也没问题（CREATE TABLE IF NOT EXISTS）";

    EXPECT_EQ(columnsOf(path, "affect_knowledge"),
              (std::vector<std::string>{"id", "kind", "scope_group", "scope_user", "key", "text",
                                        "search_text", "strength", "status", "version", "created_at",
                                        "updated_at", "last_confirmed_at"}));
    EXPECT_EQ(columnsOf(path, "affect_knowledge_history"),
              (std::vector<std::string>{"id", "knowledge_id", "version", "text", "search_text",
                                        "strength", "status", "change", "origin", "evidence_json",
                                        "run_id", "created_at"}));
}

// ---------- 版本与历史 ----------

TEST(KnowledgeLedgerTest, EveryChangeBumpsVersionAndAppendsHistory)
{
    LedgerHarness harness;
    const affect::KnowledgeRunResult added = harness.ledger.apply(
        {change("add", "habit", "venting", "别人倒苦水时，我只接一句体谅的话", kGroup)},
        harness.reflection());
    ASSERT_TRUE(added.ok);
    ASSERT_EQ(added.written.size(), 1U);
    const std::int64_t id = added.written.front();
    auto entry = harness.ledger.find(id);
    ASSERT_TRUE(entry);
    EXPECT_EQ(entry->version, 1);
    EXPECT_EQ(entry->status, "active");
    EXPECT_DOUBLE_EQ(entry->strength, affect::KnowledgeRules::kReflectionStrength);
    EXPECT_EQ(entry->lastConfirmedAt, harness.now);

    harness.now += 60;
    ASSERT_TRUE(harness.ledger.apply({change("strengthen", "habit", "venting", "", kGroup)},
                                     harness.reflection()).ok);
    harness.now += 60;
    ASSERT_TRUE(harness.ledger.apply(
        {change("modify", "habit", "venting", "别人倒苦水时，我先听着，不急着给建议", kGroup)},
        harness.reflection()).ok);
    harness.now += 60;
    ASSERT_TRUE(harness.ledger.apply({change("weaken", "habit", "venting", "", kGroup)},
                                     harness.reflection()).ok);

    entry = harness.ledger.find(id);
    EXPECT_EQ(entry->version, 4);
    EXPECT_EQ(entry->text, "别人倒苦水时，我先听着，不急着给建议");
    EXPECT_NEAR(entry->strength, 0.6, 1e-9) << "0.6 → 0.8 → 0.8（改写不降）→ 0.6";
    const auto rows = harness.ledger.history(id);
    ASSERT_EQ(rows.size(), 4U);
    EXPECT_EQ(rows[0].change, "weaken");
    EXPECT_EQ(rows[1].change, "modify");
    EXPECT_EQ(rows[2].change, "strengthen");
    EXPECT_EQ(rows[3].change, "add");
    EXPECT_EQ(rows[3].text, "别人倒苦水时，我只接一句体谅的话") << "历史保留旧正文";
    for (const auto &row : rows)
        EXPECT_EQ(row.origin, "reflection");
    EXPECT_NE(rows[0].runId, rows[1].runId) << "每次运行一个运行号";
    EXPECT_NE(rows[3].evidenceJson.find("decision:dec_1"), std::string::npos);

    // 回想“新增”已有的 key：同一句算印证，不同的算改写
    ASSERT_TRUE(harness.ledger.apply(
        {change("add", "habit", "venting", "别人倒苦水时，我先听着，不急着给建议", kGroup)},
        harness.reflection()).ok);
    EXPECT_EQ(harness.ledger.history(id).front().change, "strengthen");

    // 撤回后不能再加强
    ASSERT_TRUE(harness.ledger.apply({change("withdraw", "habit", "venting", "", kGroup)},
                                     harness.reflection()).ok);
    const auto again = harness.ledger.apply({change("strengthen", "habit", "venting", "", kGroup)},
                                            harness.reflection());
    ASSERT_EQ(again.rejected.size(), 1U);
    EXPECT_EQ(harness.ledger.find(id)->status, "withdrawn");
}

// ---------- 代码校验 ----------

TEST(KnowledgeGuardTest, RejectsLengthScopeEvidenceAndQuotesButKeepsTheRest)
{
    LedgerHarness harness;
    affect::KnowledgeRunContext context = harness.reflection();
    context.material = {"[15:51] 老余: 我们部门天天没活干，被边缘化了"};

    std::vector<affect::KnowledgeChange> changes;
    changes.push_back(change("add", "habit", "ok", "有人抱怨工作时我不急着给建议", kGroup)); // 0 合格
    changes.push_back(change("add", "habit", "long", repeated("字", 81), kGroup));            // 1 超长
    changes.push_back(change("add", "group", "other", "那个群爱斗图", kOtherGroup));          // 2 别的群
    changes.push_back(change("add", "person", "laoyu", "老余喜欢被接梗", kGroup, kLaoyu));    // 3 不在材料里
    changes.push_back(change("add", "habit", "global", "晚上少说话", kOtherGroup));          // 4 habit 别的群
    affect::KnowledgeChange noEvidence = change("add", "habit", "noev", "一句", kGroup);
    noEvidence.evidence.clear();
    changes.push_back(noEvidence);                                                            // 5 没依据
    affect::KnowledgeChange missing = change("add", "habit", "missing", "一句", kGroup);
    missing.evidence = {"decision:missing"};
    changes.push_back(missing);                                                               // 6 依据不存在
    changes.push_back(change("add", "habit", "quote", "老余说部门天天没活干，被边缘化了", kGroup)); // 7 照抄
    changes.push_back(change("add", "person", "aqing", "阿青说话直，不用绕弯", kGroup, kAqing)); // 8 合格但超 8 条
    const affect::KnowledgeRunResult result = harness.ledger.apply(changes, context);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.written.size(), 1U) << "只有第一条合格";
    ASSERT_EQ(result.rejected.size(), 8U);
    EXPECT_EQ(result.rejected[0].reason, "正文超过 80 个字");
    EXPECT_EQ(result.rejected[1].reason, "群认识只能写本群");
    EXPECT_EQ(result.rejected[2].reason, "这个人不在本次材料里");
    EXPECT_EQ(result.rejected[3].reason, "习惯只能是全局的或本群的");
    EXPECT_EQ(result.rejected[4].reason, "回想的改动必须带依据");
    EXPECT_EQ(result.rejected[5].reason, "依据不存在");
    EXPECT_EQ(result.rejected[6].reason, "照抄了群聊原文");
    EXPECT_EQ(result.rejected[7].reason, "每次至多 8 条改动");
    EXPECT_EQ(result.rejected[7].index, 8U);

    std::string reason;
    affect::KnowledgeChange twoLines = change("add", "habit", "k", "第一行\n第二行");
    EXPECT_FALSE(affect::KnowledgeGuard::check(twoLines, context, reason));
    affect::KnowledgeChange spacedKey = change("add", "habit", "a key", "一句");
    EXPECT_FALSE(affect::KnowledgeGuard::check(spacedKey, context, reason));
    affect::KnowledgeChange badKind = change("add", "rule", "k", "一句");
    EXPECT_FALSE(affect::KnowledgeGuard::check(badKind, context, reason));
    affect::KnowledgeChange badEvidence = change("add", "habit", "k", "一句");
    badEvidence.evidence = {"message:1"};
    EXPECT_FALSE(affect::KnowledgeGuard::check(badEvidence, context, reason));
    // 恰好 80 个字可以
    EXPECT_TRUE(affect::KnowledgeGuard::check(change("add", "habit", "k", repeated("字", 80)), context, reason))
        << reason;
}

// 依据核对器：decision:<id> 查决策表，feedback:<id> 查反馈表
TEST(KnowledgeGuardTest, EvidenceCheckerLooksUpDecisionsAndFeedback)
{
    TemporaryDirectory directory;
    affect::DecisionStore store(directory.path() + "/affect.db");
    affect::DecisionRecorder recorder(store, affectOn());
    const std::string decisionId =
        recorder.recordDecision(affect::makeBaselineDecision("engagement_turn", kGroup, 0, "", "group_say"));
    ASSERT_FALSE(decisionId.empty());
    affect::FeedbackRecord feedback;
    feedback.feedbackId = "fbk_1";
    feedback.decisionId = decisionId;
    feedback.source = "in_the_moment";
    feedback.status = "known";
    ASSERT_TRUE(recorder.recordFeedback(feedback));

    const auto exists = affect::evidenceCheckerFor(store);
    EXPECT_TRUE(exists("decision:" + decisionId));
    EXPECT_TRUE(exists("feedback:fbk_1"));
    EXPECT_FALSE(exists("decision:dec_nope"));
    EXPECT_FALSE(exists("feedback:fbk_nope"));
    EXPECT_FALSE(exists(decisionId));
}

// ---------- 教导不可被回想改写 ----------

TEST(KnowledgeLedgerTest, ReflectionCannotChangeWhatTeachingWrote)
{
    LedgerHarness harness;
    const auto taught = harness.ledger.apply(
        {change("add", "habit", "venting", "别人倒苦水时只接一句体谅的话")}, harness.teaching(kGroup));
    ASSERT_EQ(taught.written.size(), 1U);
    const std::int64_t id = taught.written.front();
    EXPECT_DOUBLE_EQ(harness.ledger.find(id)->strength, affect::KnowledgeRules::kTeachingStrength);
    EXPECT_EQ(harness.ledger.authorOf(id), "teaching");

    for (const char *op : {"add", "modify", "strengthen", "weaken", "withdraw"})
    {
        const auto attempt = harness.ledger.apply(
            {change(op, "habit", "venting", "别人倒苦水时给三条建议")}, harness.reflection());
        ASSERT_EQ(attempt.rejected.size(), 1U) << op;
        EXPECT_EQ(attempt.rejected.front().reason, "教导过的认识，回想不能改") << op;
    }
    EXPECT_EQ(harness.ledger.find(id)->version, 1) << "一次都没动";

    // 教导的不衰减：120 天后仍有效、有效强度 1.0；回想仍不能动
    harness.now += 120 * kDay;
    EXPECT_EQ(harness.ledger.applyDecay(), 0U);
    EXPECT_EQ(harness.ledger.find(id)->status, "active");
    EXPECT_DOUBLE_EQ(harness.ledger.effectiveOf(*harness.ledger.find(id)), 1.0);
    EXPECT_EQ(harness.ledger.authorOf(id), "teaching");
    EXPECT_EQ(harness.ledger.apply({change("strengthen", "habit", "venting", "")}, harness.reflection())
                  .rejected.size(),
              1U);

    // 新的教导可以改（按编号），也能让它恢复
    affect::KnowledgeChange edit;
    edit.op = "modify";
    edit.targetId = id;
    edit.text = "别人倒苦水时先听着";
    const auto edited = harness.ledger.apply({edit}, harness.teaching());
    ASSERT_EQ(edited.written.size(), 1U);
    EXPECT_EQ(harness.ledger.find(id)->status, "active");
    EXPECT_EQ(harness.ledger.find(id)->text, "别人倒苦水时先听着");

    // 回滚到教导之前：这一条记为撤回，作者变成回滚，回想可以重新写这个 key
    const auto rolled = harness.ledger.rollbackBefore(taught.runId);
    ASSERT_TRUE(rolled.ok);
    EXPECT_EQ(harness.ledger.find(id)->status, "withdrawn");
    EXPECT_EQ(harness.ledger.authorOf(id), "rollback");
    EXPECT_EQ(harness.ledger.apply({change("add", "habit", "venting", "回想写的一句")}, harness.reflection())
                  .written.size(),
              1U);

    // 回滚到教导那一版：作者恢复为教导，回想又不能动
    const auto restore = harness.ledger.rollbackBefore(edited.runId);
    ASSERT_TRUE(restore.ok);
    EXPECT_EQ(harness.ledger.find(id)->text, "别人倒苦水时只接一句体谅的话");
    EXPECT_EQ(harness.ledger.authorOf(id), "teaching");
    EXPECT_EQ(harness.ledger.apply({change("withdraw", "habit", "venting", "")}, harness.reflection())
                  .rejected.size(),
              1U);
}

// ---------- 回滚 ----------

TEST(KnowledgeLedgerTest, RollbackToATimeOrBeforeARunOnlyAppendsHistory)
{
    LedgerHarness harness;
    const std::int64_t t1 = harness.now;
    const auto first = harness.ledger.apply({change("add", "habit", "a", "第一版")}, harness.reflection());
    const std::int64_t a = first.written.front();
    harness.now += 3600;
    const auto second = harness.ledger.apply({change("modify", "habit", "a", "第二版")}, harness.reflection());
    harness.now += 3600;
    const auto third = harness.ledger.apply({change("add", "group", "b", "这个群爱斗图", kGroup)},
                                            harness.reflection());
    const std::int64_t b = third.written.front();
    const std::size_t before = historyRowCount(harness.directory.path() + "/affect.db");

    harness.now += 3600;
    const auto toT1 = harness.ledger.rollbackTo(t1);
    ASSERT_TRUE(toT1.ok);
    EXPECT_EQ(toT1.changed, 2U);
    EXPECT_EQ(harness.ledger.find(a)->text, "第一版");
    EXPECT_EQ(harness.ledger.find(a)->status, "active");
    EXPECT_EQ(harness.ledger.find(a)->version, 3);
    EXPECT_EQ(harness.ledger.find(a)->lastConfirmedAt, t1) << "上次印证回到那一版的时刻";
    EXPECT_EQ(harness.ledger.find(b)->status, "withdrawn") << "那时还没有的，不删，记为撤回";
    const auto rows = harness.ledger.history(a);
    EXPECT_EQ(rows.front().change, "rollback");
    EXPECT_EQ(rows.front().origin, "rollback");
    EXPECT_EQ(rows.front().runId, toT1.runId);
    EXPECT_NE(rows.front().evidenceJson.find("\"to_version\":1"), std::string::npos);
    EXPECT_EQ(historyRowCount(harness.directory.path() + "/affect.db"), before + 2) << "历史只增不删";

    // 再回滚到第二次运行之前 = 同一状态：没有改动
    EXPECT_EQ(harness.ledger.rollbackBefore(second.runId).changed, 0U);
    // 回到第三次运行之前：a 是第二版，b 仍不存在（已是撤回，不再写）
    const auto beforeThird = harness.ledger.rollbackBefore(third.runId);
    EXPECT_EQ(beforeThird.changed, 1U);
    EXPECT_EQ(harness.ledger.find(a)->text, "第二版");
    // 没有这次运行
    const auto unknown = harness.ledger.rollbackBefore("run_nope");
    EXPECT_TRUE(unknown.ok);
    EXPECT_FALSE(unknown.runFound);
    EXPECT_EQ(unknown.changed, 0U);
    (void)first;
}

// ---------- 遗忘 ----------

TEST(KnowledgeLedgerTest, DecayHalvesEveryThirtyDaysAndFadesBelowThreshold)
{
    EXPECT_DOUBLE_EQ(affect::effectiveStrength(0.8, 0, 30 * kDay), 0.4);
    EXPECT_DOUBLE_EQ(affect::effectiveStrength(0.8, 0, 60 * kDay), 0.2);
    EXPECT_DOUBLE_EQ(affect::effectiveStrength(0.8, 10, 0), 0.8) << "时间倒流按 0 天";

    LedgerHarness harness;
    const auto added = harness.ledger.apply({change("add", "habit", "a", "一句")}, harness.reflection());
    const std::int64_t id = added.written.front();
    harness.now += 59 * kDay; // 0.6 × 0.5^(59/30) ≈ 0.154
    EXPECT_EQ(harness.ledger.applyDecay(), 0U);
    EXPECT_EQ(harness.ledger.candidatesFor(kGroup, {}).size(), 1U);
    harness.now += 2 * kDay; // ≈ 0.147
    EXPECT_TRUE(harness.ledger.candidatesFor(kGroup, {}).empty()) << "取用只看有效强度，不等遗忘落账";
    EXPECT_EQ(harness.ledger.applyDecay(), 1U);
    auto entry = harness.ledger.find(id);
    EXPECT_EQ(entry->status, "faded");
    EXPECT_DOUBLE_EQ(entry->strength, 0.6) << "存储强度不变，只是不再取用";
    EXPECT_EQ(harness.ledger.history(id).front().change, "fade");
    EXPECT_EQ(harness.ledger.history(id).front().origin, "decay");
    EXPECT_EQ(harness.ledger.applyDecay(), 0U) << "淡了的不重复记";

    // 再次印证：恢复
    ASSERT_EQ(harness.ledger.apply({change("strengthen", "habit", "a", "")}, harness.reflection())
                  .written.size(),
              1U);
    entry = harness.ledger.find(id);
    EXPECT_EQ(entry->status, "active");
    EXPECT_EQ(entry->lastConfirmedAt, harness.now);
    EXPECT_EQ(harness.ledger.candidatesFor(kGroup, {}).size(), 1U);

    // 教导的不衰减：120 天后教导的仍有效（有效强度 1.0、仍可取用），同时写下的回想认识已淡
    const auto taught = harness.ledger.apply({change("add", "habit", "taught", "教的一句")},
                                             harness.teaching());
    const auto learned = harness.ledger.apply({change("add", "habit", "learned", "回想的一句")},
                                              harness.reflection());
    const std::int64_t taughtId = taught.written.front();
    const std::int64_t learnedId = learned.written.front();
    harness.now += 120 * kDay;
    EXPECT_EQ(harness.ledger.applyDecay(), 2U) << "回想写的两条（a 与 learned）淡了";
    EXPECT_EQ(harness.ledger.find(taughtId)->status, "active");
    EXPECT_DOUBLE_EQ(harness.ledger.effectiveOf(*harness.ledger.find(taughtId)), 1.0);
    EXPECT_EQ(harness.ledger.find(learnedId)->status, "faded");
    std::vector<std::string> texts;
    for (const auto &candidate : harness.ledger.candidatesFor(kGroup, {}))
        texts.push_back(candidate.text);
    EXPECT_EQ(texts, std::vector<std::string>{"教的一句"});
}

// ---------- 规模上限 ----------

TEST(KnowledgeLedgerTest, ScopeKeepsAtMostTwoHundredActiveWeakestFadesFirstTaughtLast)
{
    LedgerHarness harness;
    const auto taught = harness.ledger.apply({change("add", "habit", "taught", "教的一句", kGroup)},
                                             harness.teaching(kGroup));
    const std::int64_t taughtId = taught.written.front();
    harness.now += 60 * kDay; // 教导的这条有效强度 0.25，是最弱的
    std::int64_t weakest = 0;
    int added = 0;
    while (added < 200)
    {
        std::vector<affect::KnowledgeChange> batch;
        for (int i = 0; i < 8 && added < 200; ++i, ++added)
            batch.push_back(change("add", "habit", "k" + std::to_string(added),
                                   "第 " + std::to_string(added) + " 条", kGroup));
        const auto result = harness.ledger.apply(batch, harness.reflection());
        ASSERT_TRUE(result.ok);
        if (weakest == 0)
        {
            weakest = result.written.front();
            ASSERT_EQ(harness.ledger.apply({change("weaken", "habit", "k0", "", kGroup)},
                                           harness.reflection()).written.size(),
                      1U);
        }
    }
    affect::KnowledgeQuery filter;
    filter.kind = "habit";
    filter.scopeGroup = kGroup;
    std::size_t active = 0, faded = 0;
    for (const auto &entry : harness.ledger.query(filter))
        (entry.status == "active" ? active : faded) += 1;
    EXPECT_EQ(active, affect::KnowledgeRules::kMaxActivePerScope);
    EXPECT_EQ(faded, 1U);
    EXPECT_EQ(harness.ledger.find(weakest)->status, "faded") << "回想写的里最弱的先淡";
    EXPECT_EQ(harness.ledger.find(taughtId)->status, "active") << "教导的最后才轮到";
    EXPECT_EQ(harness.ledger.history(weakest).front().evidenceJson, R"({"reason":"scope_limit"})");

    // 其他作用域不受影响
    EXPECT_EQ(harness.ledger.apply({change("add", "habit", "global", "全局的一句")}, harness.reflection())
                  .scopeFaded,
              0U);
}

// ---------- 按人、按群清除 ----------

TEST(KnowledgeLedgerTest, ForgetPersonOrGroupRemovesEntriesWithTheirHistory)
{
    LedgerHarness harness;
    harness.ledger.apply({change("add", "person", "aqing", "阿青说话直", kGroup, kAqing),
                          change("add", "group", "memes", "这个群晚上斗图", kGroup),
                          change("add", "habit", "night", "晚上少说话")},
                         harness.reflection());
    affect::KnowledgeRunContext other = harness.teaching(kOtherGroup);
    harness.ledger.apply({change("add", "person", "aqing2", "阿青在那个群也在", kOtherGroup, kAqing)}, other);
    ASSERT_EQ(harness.ledger.query({}).size(), 4U);

    EXPECT_EQ(harness.ledger.forgetPerson(kAqing), 2U);
    EXPECT_EQ(harness.ledger.forgetGroup(kGroup), 1U);
    const auto left = harness.ledger.query({});
    ASSERT_EQ(left.size(), 1U);
    EXPECT_EQ(left.front().key, "night");
    EXPECT_EQ(historyRowCount(harness.directory.path() + "/affect.db"), 1U) << "历史一起删";
    EXPECT_EQ(harness.ledger.forgetPerson(kAqing), 0U);
}

// ---------- 取用候选（E4 的读接口） ----------

TEST(KnowledgeLedgerTest, CandidatesAreGlobalHabitsThisGroupAndPresentPeopleActiveOnly)
{
    LedgerHarness harness;
    affect::KnowledgeRunContext here = harness.teaching(kGroup);
    harness.ledger.apply({change("add", "habit", "global", "全局习惯"),
                          change("add", "habit", "here", "本群习惯", kGroup),
                          change("add", "group", "group", "本群认识", kGroup),
                          change("add", "person", "aqing", "阿青", kGroup, kAqing),
                          change("add", "person", "laoyu", "老余", 0, kLaoyu),
                          change("add", "habit", "gone", "撤回的")},
                         here);
    harness.ledger.apply({change("withdraw", "habit", "gone", "")}, here);
    affect::KnowledgeRunContext there = harness.teaching(kOtherGroup);
    harness.ledger.apply({change("add", "habit", "there", "别的群习惯", kOtherGroup),
                          change("add", "group", "there", "别的群认识", kOtherGroup),
                          change("add", "person", "aqing", "阿青在别的群", kOtherGroup, kAqing)},
                         there);
    harness.now += 1;
    harness.ledger.apply({change("weaken", "habit", "here", "", kGroup)}, here); // 1.0 → 0.8

    std::vector<std::string> texts;
    for (const auto &entry : harness.ledger.candidatesFor(kGroup, {kAqing}))
        texts.push_back(entry.text);
    EXPECT_EQ(texts, (std::vector<std::string>{"本群认识", "全局习惯", "阿青", "本群习惯"}))
        << "强的在前（一样强按 kind、范围、编号）；别的群、撤回的、不在场的人都不在";

    texts.clear();
    for (const auto &entry : harness.ledger.candidatesFor(kGroup, {kAqing, kLaoyu}))
        texts.push_back(entry.text);
    EXPECT_EQ(texts.size(), 5U) << "老余的全局认识在他在场时也取";

    texts.clear();
    for (const auto &entry : harness.ledger.candidatesFor(0, {}))
        texts.push_back(entry.text);
    EXPECT_EQ(texts, (std::vector<std::string>{"全局习惯"})) << "私聊只有全局习惯";
}

// ---------- 隐私 ----------

TEST(KnowledgeLedgerTest, LedgerHoldsOnlyDerivedTextAndEvidenceIds)
{
    LedgerHarness harness;
    affect::KnowledgeRunContext context = harness.reflection();
    const std::string original = "我们部门天天没活干被边缘化了真烦";
    context.material = {"[10:47] 阿青: " + original};
    affect::KnowledgeChange derived = change("add", "habit", "venting", "有人抱怨工作时我只接一句", kGroup);
    derived.why = "上次接了一句体谅的话，她接着说下去了";
    affect::KnowledgeChange quoted = change("add", "habit", "quoted", "有人说" + original, kGroup);
    affect::KnowledgeChange quotedSearch = change("add", "habit", "q2", "有人抱怨", kGroup);
    quotedSearch.searchText = "部门天天没活干被边缘化";
    const auto result = harness.ledger.apply({derived, quoted, quotedSearch}, context);
    ASSERT_EQ(result.written.size(), 1U);
    const std::string all = allLedgerText(harness.directory.path() + "/affect.db");
    EXPECT_EQ(all.find("没活干"), std::string::npos) << all;
    EXPECT_EQ(all.find("边缘化"), std::string::npos);
    EXPECT_NE(all.find("decision:dec_1"), std::string::npos) << "依据只存 id";
}

// ---------- 当场察觉 ----------

TEST(InTheMomentTest, ReadingBecomesFeedbackOnThePreviousDecision)
{
    affect::BehaviorPlan plan;
    plan.schemaVersion = affect::BehaviorPlan::kSchemaVersionV2;
    plan.action = "no_reply";
    plan.occasion = "群友在聊别的";
    plan.reactionToLast = "  我上一句之后阿青接着说了下去，算接住了 ";

    const auto feedback = affect::inTheMomentFeedback("dec_prev", "dec_now", plan);
    ASSERT_TRUE(feedback);
    EXPECT_EQ(feedback->decisionId, "dec_prev");
    EXPECT_EQ(feedback->source, "in_the_moment");
    EXPECT_EQ(feedback->status, "known");
    EXPECT_DOUBLE_EQ(feedback->confidence, 0.0) << "代码不打分";
    EXPECT_EQ(nlohmann::json::parse(feedback->evidenceJson),
              (nlohmann::json{{"reading", "我上一句之后阿青接着说了下去，算接住了"},
                              {"observed_in", "dec_now"}}));
    EXPECT_FALSE(feedback->evidenceJson.find("label") != std::string::npos) << "不冒充 #好/#坏";

    EXPECT_FALSE(affect::inTheMomentFeedback("", "dec_now", plan)) << "她这次会话还没开过口";
    EXPECT_FALSE(affect::inTheMomentFeedback("dec_now", "dec_now", plan));
    affect::BehaviorPlan empty = plan;
    empty.reactionToLast = "  ";
    EXPECT_FALSE(affect::inTheMomentFeedback("dec_prev", "dec_now", empty));
    affect::BehaviorPlan v1 = plan;
    v1.schemaVersion = 1;
    EXPECT_FALSE(affect::inTheMomentFeedback("dec_prev", "dec_now", v1));
}

// ---------- 账本工具（管理员私聊） ----------

// 三个工具只在管理员私聊开放：群里、别人的私聊里工具表没有它们；场合按会话与 bot.manager_id 判定
TEST(KnowledgeToolTest, OfferedOnlyInTheManagersPrivateChat)
{
    LedgerHarness harness;
    RememberGuidanceAction remember(harness.ledger, affectOn());
    ReviewGuidanceAction review(harness.ledger, affectOn());
    ForgetGuidanceAction forget(harness.ledger, affectOn());
    ToolRegistry registry;
    registry.registerTool(std::make_unique<ActionTool>(remember));
    registry.registerTool(std::make_unique<ActionTool>(review));
    registry.registerTool(std::make_unique<ActionTool>(forget));

    const std::vector<std::string> all{"remember_guidance", "review_guidance", "forget_guidance"};
    EXPECT_EQ(registry.namesIn(ToolVenue::ManagerPrivate), all);
    EXPECT_TRUE(registry.namesIn(ToolVenue::Private).empty()) << "别人的私聊里没有";
    EXPECT_TRUE(registry.namesIn(ToolVenue::Group).empty()) << "群里没有";

    EXPECT_EQ(venueOf(0, kManager, kManager), ToolVenue::ManagerPrivate);
    EXPECT_EQ(venueOf(0, kAqing, kManager), ToolVenue::Private);
    EXPECT_EQ(venueOf(kGroup, kManager, kManager), ToolVenue::Group) << "管理员在群里也不算";
    EXPECT_EQ(venueOf(0, 0, 0), ToolVenue::Private) << "没配管理员就没有管理员私聊";

    for (const Action *action : std::vector<const Action *>{&remember, &review, &forget})
    {
        EXPECT_TRUE(action->descriptor().expose_as_tool);
        EXPECT_TRUE(action->descriptor().requires_admin) << "工具循环再按调用者挡一道";
        EXPECT_TRUE(action->usageGuidance(GuidanceScene::Conversation, {}).empty())
            << "不往 system prompt 里加规则";
    }
    // 一般工具在两种私聊里一样开放
    struct Plain : Action
    {
        const ActionDescriptor &descriptor() const override
        {
            static const ActionDescriptor value{"plain", "", {{"type", "object"}}, true, false};
            return value;
        }
        ActionResult execute(const nlohmann::json &, const ActionContext &) override { return {}; }
    } plain;
    ActionTool plainTool(plain);
    EXPECT_TRUE(plainTool.offeredIn(ToolVenue::Private));
    EXPECT_TRUE(plainTool.offeredIn(ToolVenue::ManagerPrivate));
    EXPECT_TRUE(plainTool.offeredIn(ToolVenue::Group));
}

// affect.enabled=false：工具表里没有（而不是给了再回“未开启”），账本不打开；开关一开下一次就有
TEST(KnowledgeToolTest, AffectOffHidesThemAndLeavesTheLedgerUntouched)
{
    TemporaryDirectory directory;
    const std::string path = directory.path() + "/affect.db";
    affect::KnowledgeLedger ledger(path);
    Live<AffectOptions> live{AffectOptions{}};
    RememberGuidanceAction remember(ledger, live);
    ReviewGuidanceAction review(ledger, live);
    ForgetGuidanceAction forget(ledger, live);
    ToolRegistry registry;
    registry.registerTool(std::make_unique<ActionTool>(remember));
    registry.registerTool(std::make_unique<ActionTool>(review));
    registry.registerTool(std::make_unique<ActionTool>(forget));

    EXPECT_TRUE(registry.namesIn(ToolVenue::ManagerPrivate).empty());
    EXPECT_FALSE(registry.schemaFor("remember_guidance").has_value());
    // 工具循环按“未开放”拒绝；直接调用也不碰账本
    EXPECT_EQ(call(remember, {{"text", "晚上少说话"}}), "功能未开启");
    EXPECT_EQ(call(review, nlohmann::json::object()), "功能未开启");
    EXPECT_EQ(call(forget, {{"id", 1}}), "功能未开启");
    EXPECT_FALSE(ledger.isOpen());
    EXPECT_FALSE(std::filesystem::exists(path));

    live.set(AffectOptions{true});
    EXPECT_EQ(registry.namesIn(ToolVenue::ManagerPrivate).size(), 3U);
    EXPECT_EQ(call(remember, {{"text", "晚上少说话"}}), "记下了（#1｜全局）：晚上少说话");
    EXPECT_TRUE(ledger.isOpen());
}

// 记下：走教导路径（origin=teaching、强度 1.0、不衰减、回想改不了），三种范围与改写
TEST(KnowledgeToolTest, RememberGoesThroughTeachingAndDoesNotFade)
{
    LedgerHarness harness;
    RememberGuidanceAction remember(harness.ledger, affectOn());

    EXPECT_EQ(call(remember, {{"text", "  别人倒苦水时只接一句体谅的话 "}}),
              "记下了（#1｜全局）：别人倒苦水时只接一句体谅的话");
    const auto first = harness.ledger.find(1);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->kind, "habit");
    EXPECT_EQ(first->scopeGroup, 0U);
    EXPECT_DOUBLE_EQ(first->strength, affect::KnowledgeRules::kTeachingStrength);
    EXPECT_EQ(harness.ledger.authorOf(1), "teaching");
    const auto rows = harness.ledger.history(1);
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].change, "add");
    EXPECT_EQ(rows[0].origin, "teaching");
    const nlohmann::json evidence = nlohmann::json::parse(rows[0].evidenceJson);
    EXPECT_EQ(evidence["by"], kManager);
    EXPECT_EQ(evidence["via"], "tool");
    EXPECT_EQ(allLedgerText(harness.directory.path() + "/affect.db").find("原话不进账本"),
              std::string::npos)
        << "只存她写下的那一句";

    EXPECT_EQ(call(remember, {{"text", "别人倒苦水时只接一句体谅的话"}, {"scope", "global"}}),
              "这句本来就记着，算再确认一次（#1｜全局）：别人倒苦水时只接一句体谅的话");
    EXPECT_EQ(call(remember, {{"text", "这个群里晚上少说话"}, {"scope", "group:882926802"}}),
              "记下了（#2｜群 882926802）：这个群里晚上少说话")
        << "私聊里也能记某个群的";
    EXPECT_EQ(harness.ledger.find(2)->kind, "habit");
    EXPECT_EQ(call(remember, {{"text", "阿青说话直，不用绕弯"}, {"scope", "user：20002"}}),
              "记下了（#3｜关于 20002）：阿青说话直，不用绕弯");
    EXPECT_EQ(harness.ledger.find(3)->kind, "person");
    EXPECT_EQ(harness.ledger.find(3)->scopeUser, kAqing);

    harness.now += 60;
    EXPECT_EQ(call(remember, {{"text", "这个群晚上常斗图，插不上话就不出声"}, {"replaces", "#2"}}),
              "改好了（#2｜群 882926802）：这个群晚上常斗图，插不上话就不出声");
    EXPECT_EQ(harness.ledger.find(2)->version, 2);

    EXPECT_EQ(call(remember, {{"text", repeated("字", 81)}}), "没记下：正文超过 80 个字。");
    EXPECT_EQ(call(remember, {{"text", "一句"}, {"scope", "群里"}}),
              "scope 只能是 global、group:<群号> 或 user:<QQ号>。");
    EXPECT_EQ(call(remember, {{"text", "一句"}, {"replaces", 99}}), "没记下：没有这一条。");
    EXPECT_EQ(call(remember, {{"text", "一句"}, {"replaces", "第二条"}}), "replaces 要填条目编号。");

    // 120 天后：教导的不衰减，遗忘不把它转淡；回想也改不了
    harness.now += 120 * kDay;
    harness.ledger.applyDecay();
    EXPECT_EQ(harness.ledger.find(1)->status, "active");
    EXPECT_DOUBLE_EQ(harness.ledger.effectiveOf(*harness.ledger.find(1)), 1.0);
    affect::KnowledgeChange reflected = change("weaken", "habit", first->key, first->text);
    const affect::KnowledgeRunResult result = harness.ledger.apply({reflected}, harness.reflection());
    EXPECT_TRUE(result.written.empty());
}

// 翻看：全部 / 某个范围 / 某一条（编号、范围、原文、强度、来源、最后确认时间）
TEST(KnowledgeToolTest, ReviewListsEntriesWithScopeStrengthOriginAndTime)
{
    LedgerHarness harness;
    RememberGuidanceAction remember(harness.ledger, affectOn());
    ReviewGuidanceAction review(harness.ledger, affectOn());
    EXPECT_EQ(call(review, nlohmann::json::object()), "认识账本里还没有条目。");

    call(remember, {{"text", "阿青说话直"}, {"scope", "user:20002"}});
    call(remember, {{"text", "这个群里晚上少说话"}, {"scope", "group:882926802"}});
    call(remember, {{"text", "别人倒苦水时只接一句"}});
    const std::string when = [&harness]
    {
        const std::tm local = localTimeOf(static_cast<std::time_t>(harness.now));
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
        return std::string(buffer);
    }();

    const std::string everything = call(review, nlohmann::json::object());
    EXPECT_EQ(everything, "认识账本（3 条）：\n"
                          "#3｜全局｜别人倒苦水时只接一句｜强度 1.00｜教导｜最后确认 " + when + "\n"
                          "#2｜群 882926802｜这个群里晚上少说话｜强度 1.00｜教导｜最后确认 " + when + "\n"
                          "#1｜关于 20002｜阿青说话直｜强度 1.00｜教导｜最后确认 " + when)
        << "全局在前，再按群、按人";
    EXPECT_EQ(call(review, {{"scope", "global"}}),
              "全局（1 条）：\n#3｜全局｜别人倒苦水时只接一句｜强度 1.00｜教导｜最后确认 " + when);
    EXPECT_NE(call(review, {{"scope", "user:20002"}}).find("阿青说话直"), std::string::npos);
    EXPECT_EQ(call(review, {{"scope", "user:30003"}}), "关于 30003：没有条目。");
    const std::string one = call(review, {{"id", 2}});
    EXPECT_NE(one.find("#2｜群 882926802｜这个群里晚上少说话"), std::string::npos) << one;
    EXPECT_NE(one.find("新增·教导｜这个群里晚上少说话"), std::string::npos) << one;
    EXPECT_EQ(call(review, {{"id", 99}}), "没有这一条。");
}

// 忘掉：按编号撤回（写历史，可查）；按人 / 按群连同历史删；全局只能按编号
TEST(KnowledgeToolTest, ForgetWithdrawsWithHistoryOrClearsAWholeScope)
{
    LedgerHarness harness;
    RememberGuidanceAction remember(harness.ledger, affectOn());
    ForgetGuidanceAction forget(harness.ledger, affectOn());
    call(remember, {{"text", "晚上少说话"}});
    call(remember, {{"text", "阿青说话直"}, {"scope", "user:20002"}});
    call(remember, {{"text", "这个群爱斗图"}, {"scope", "group:882926802"}});

    harness.now += 60;
    EXPECT_EQ(call(forget, {{"id", 1}}), "#1 不作数了（变化记录还在）：晚上少说话");
    EXPECT_EQ(harness.ledger.find(1)->status, "withdrawn");
    const auto rows = harness.ledger.history(1);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].change, "withdraw");
    EXPECT_EQ(rows[0].origin, "teaching");
    EXPECT_EQ(call(forget, {{"id", 1}}), "没忘掉：这一条已经撤回了。");
    EXPECT_EQ(call(forget, {{"id", 99}}), "没忘掉：没有这一条。");

    EXPECT_EQ(call(forget, {{"scope", "global"}}), "全局的只能按 id 一条一条撤回。");
    EXPECT_EQ(call(forget, {{"id", 2}, {"scope", "user:20002"}}), "id 和 scope 只给一个。");
    EXPECT_EQ(call(forget, nlohmann::json::object()), "id 和 scope 只给一个。");
    EXPECT_EQ(call(forget, {{"scope", "user:20002"}}), "关于 20002 的 1 条删了（连同变化记录）。");
    EXPECT_FALSE(harness.ledger.find(2));
    EXPECT_TRUE(harness.ledger.history(2).empty());
    EXPECT_EQ(call(forget, {{"scope", "group:882926802"}}), "群 882926802 的 1 条删了（连同变化记录）。");
    EXPECT_EQ(call(forget, {{"scope", "group:882926802"}}), "群 882926802 本来就没有条目。");
}
