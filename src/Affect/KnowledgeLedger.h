#ifndef AFFECT_KNOWLEDGE_LEDGER_H
#define AFFECT_KNOWLEDGE_LEDGER_H

/*
 * 认识账本（经历层 E2，ADR-028；设计 §6.3）：她从经历里整理出的认识，当前值 + 只增不删的历史。
 * 照搬长期记忆结构化事实层的做法（memory_current_facts / memory_fact_history），存储归情感模块
 * 自己：表 affect_knowledge（当前）与 affect_knowledge_history（每次变化一行），与决策库同一个
 * SQLite 文件、独立连接。
 *
 * 延迟打开（ADR-023 稳定外壳，与 DecisionStore 同一做法）：对象始终构造，构造时不碰数据库；第一次
 * open() 才打开连接、建表。组合根里只有账本工具在 affect.enabled 为真时调用 open()，没启用过就没有这两张表；
 * 打开失败只试一次。未打开时读写方法返回失败 / 空结果，不会自行打开。
 *
 * 写入只有一个入口 apply()：一次运行（一次回想、一次教导）的若干条改动先经 KnowledgeGuard 的代码校验，
 * 再按账本状态检查（教导过的认识回想不能动），合格的在一个事务里写入，带同一个运行号；不合格的单条
 * 丢弃并说明原因，其余照常写入。回滚、遗忘、规模上限都只追加历史行，历史永不删除；唯一例外是按人、
 * 按群清除（forgetPerson / forgetGroup），与长期记忆的 clearUser 一样连同历史一起删。
 *
 * 隐私（设计 §8）：账本里只有她（或部署者教她）的派生文字与依据 id，没有群友原文；本类的日志只写
 * 条数与 id，不写正文。
 */
#include "../../Library/nlohmann/json.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct sqlite3;

namespace affect
{
// 账本的规则常量：资源预算与安全护栏（设计 §6.2 / §6.3），不是配置。
// 遗忘（半衰期、阈值）只作用于回想写的认识：作者是教导的认识不衰减（有效强度 = 存储强度），
// 不会被遗忘转淡——部署者的纠正分量最高；它仍计入每作用域 200 条的上限，超限时最后才转淡
struct KnowledgeRules
{
    static constexpr std::size_t kMaxTextCodepoints = 80;        // 认识正文
    static constexpr std::size_t kMaxSearchTextCodepoints = 120; // 检索词
    static constexpr std::size_t kMaxKeyBytes = 64;
    static constexpr std::size_t kMaxChangesPerRun = 8;
    static constexpr std::size_t kMaxActivePerScope = 200;
    static constexpr double kHalfLifeDays = 30.0;
    static constexpr double kFadeThreshold = 0.15;  // 有效强度低于它记为 faded，不再取用
    static constexpr double kTeachingStrength = 1.0; // 教导写入与改写的强度
    static constexpr double kReflectionStrength = 0.6; // 回想新增的初始强度
    static constexpr double kStrengthStep = 0.2;       // strengthen / weaken 一次的幅度
};

// 有效强度 = 强度 × 0.5^(距上次印证天数 / 30)；时间倒流按 0 天算
double effectiveStrength(double strength, std::int64_t lastConfirmedAt, std::int64_t now);

// 当前表一行
struct KnowledgeEntry
{
    std::int64_t id = 0;     // 编号（账本工具的 id / replaces）
    std::string kind;        // habit / group / person
    std::uint64_t scopeGroup = 0; // 0 = 全局
    std::uint64_t scopeUser = 0;  // 只有 person 非 0
    std::string key;
    std::string text;        // 第一人称的一句认识
    std::string searchText;  // 检索词（E4 取用时的文本召回用）
    double strength = 0.0;   // 0–1，存储值；取用看 effectiveStrength
    std::string status;      // active / faded / withdrawn
    int version = 0;
    std::int64_t createdAt = 0;
    std::int64_t updatedAt = 0;
    std::int64_t lastConfirmedAt = 0;

    double effective(std::int64_t now) const { return effectiveStrength(strength, lastConfirmedAt, now); }
    // 按作者算的有效强度：作者是 teaching 时不衰减，等于存储强度
    double effectiveFor(const std::string &author, std::int64_t now) const
    {
        return author == "teaching" ? strength : effective(now);
    }
};

// 历史表一行：每次变化的结果状态
struct KnowledgeHistoryRow
{
    std::int64_t id = 0;
    std::int64_t knowledgeId = 0;
    int version = 0;
    std::string text;
    std::string searchText;
    double strength = 0.0;
    std::string status;
    std::string change; // add / modify / strengthen / weaken / fade / withdraw / rollback
    std::string origin; // reflection / teaching / decay / rollback
    std::string evidenceJson = "{}";
    std::string runId;
    std::int64_t createdAt = 0;
};

// 一条改动请求（回想的输出、教导工具都翻成它）
struct KnowledgeChange
{
    std::string op; // add / modify / strengthen / weaken / withdraw
    std::string kind;
    std::uint64_t scopeGroup = 0;
    std::uint64_t scopeUser = 0;
    std::string key;
    std::string text;
    std::string searchText;
    std::vector<std::string> evidence; // "decision:<id>" / "feedback:<id>"
    std::string why;
    // 非 0 时按编号找已有条目（教导改写、撤回某一条），kind/scope/key 取自那一条；
    // 那一条的作用域按本次所在的群再查一次（私聊里的教导不查）
    std::int64_t targetId = 0;
};

// 一次运行的上下文
struct KnowledgeRunContext
{
    std::string origin;        // reflection / teaching
    std::uint64_t groupId = 0; // 本次所在的群；私聊教导为 0
    // 本次材料里出现过的人（回想写 person 认识时必须在内）；教导不检查
    std::set<std::uint64_t> presentUsers;
    // 本次材料的原文（只在内存里，用来挡照抄）；教导为空
    std::vector<std::string> material;
    // 依据 id 是否存在（组合根 / E3 接 DecisionStore）；空 = 没有核对器，带依据的改动一律不收
    std::function<bool(const std::string &)> evidenceExists;
    std::string runId;                                   // 空 = 由账本生成
    nlohmann::json note = nlohmann::json::object();      // 并入每行 evidence_json（如 by=部署者号）
};

struct RejectedChange
{
    std::size_t index = 0; // 在请求里的下标
    std::string reason;
};

struct KnowledgeRunResult
{
    bool ok = false;          // 账本可用且事务提交
    std::string error;
    std::string runId;
    std::vector<std::int64_t> written; // 与 applied 一一对应：写入的条目编号
    std::vector<std::string> applied;  // 实际记下的 change（add / modify / strengthen …）
    std::vector<RejectedChange> rejected;
    std::size_t scopeFaded = 0; // 因规模上限转为 faded 的条数
};

struct KnowledgeRollbackResult
{
    bool ok = false;
    bool runFound = true; // 按运行号回滚时：账本里有没有这次运行
    std::string error;
    std::string runId;    // 本次回滚自己的运行号
    std::size_t changed = 0;
};

// 查询条件：空 = 不限
struct KnowledgeQuery
{
    std::optional<std::string> kind;
    std::optional<std::uint64_t> scopeGroup;
    std::optional<std::uint64_t> scopeUser;
    bool includeWithdrawn = false;
};

class KnowledgeLedger
{
public:
    explicit KnowledgeLedger(std::string dbPath, std::function<std::int64_t()> clock = {});
    ~KnowledgeLedger();

    KnowledgeLedger(const KnowledgeLedger &) = delete;
    KnowledgeLedger &operator=(const KnowledgeLedger &) = delete;

    // 打开连接并建表（幂等、线程安全）；返回是否可用
    bool open();
    bool isOpen() const;

    // 写入的唯一入口：校验 → 账本状态检查 → 一个事务写入
    KnowledgeRunResult apply(const std::vector<KnowledgeChange> &changes,
                             const KnowledgeRunContext &context);

    // 回滚：恢复到某一时刻（created_at ≤ at 的历史为准）/ 某次运行之前的状态。
    // 只追加 rollback 历史行；那时还不存在的条目记为 withdrawn
    KnowledgeRollbackResult rollbackTo(std::int64_t at);
    KnowledgeRollbackResult rollbackBefore(const std::string &runId);

    // 遗忘：有效强度低于阈值的 active 条目转为 faded（追加 fade / decay 历史行）；返回条数。
    // 作者是教导的条目不衰减、不会被遗忘转淡
    std::size_t applyDecay();

    // 按人 / 按群清除（连同历史一起删，同长期记忆的 clearUser）；返回删掉的条目数
    std::size_t forgetPerson(std::uint64_t userId);
    std::size_t forgetGroup(std::uint64_t groupId);

    // ---- 读 ----
    std::optional<KnowledgeEntry> find(std::int64_t id) const;
    std::vector<KnowledgeEntry> query(const KnowledgeQuery &query) const;
    // 某条的历史（新的在前）
    std::vector<KnowledgeHistoryRow> history(std::int64_t id) const;
    // 当前起作用的作者：最近一次来自回想或教导的改动的来源（遗忘行跳过，回滚行按它恢复到的那一版算）；
    // 没有为空串，回滚到“还不存在”为 rollback
    std::string authorOf(std::int64_t id) const;
    // 这一条此刻的有效强度（按作者：教导的不衰减）；翻看工具显示用
    double effectiveOf(const KnowledgeEntry &entry) const;

    // 取用候选（设计 §7，E4 调用；E2 只由测试调用）：全局习惯 + 本群的群认识与习惯 + 在场的人的认识
    //（person 的 scope_group 为 0 或本群），只取 active 且有效强度（教导的不衰减）不低于阈值；按有效强度降序。
    // 不做文本召回与预算——那是 E4 的 KnowledgeRecall
    std::vector<KnowledgeEntry> candidatesFor(std::uint64_t groupId,
                                              const std::vector<std::uint64_t> &people) const;

private:
    std::int64_t now() const { return clock(); }

    const std::string path;
    const std::function<std::int64_t()> clock;
    sqlite3 *db = nullptr;
    bool openAttempted = false;
    mutable std::mutex mutex;
};
} // namespace affect

#endif // AFFECT_KNOWLEDGE_LEDGER_H
