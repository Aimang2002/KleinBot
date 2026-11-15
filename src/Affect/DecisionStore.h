#ifndef AFFECT_DECISION_STORE_H
#define AFFECT_DECISION_STORE_H

#include "DecisionRecord.h"

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace affect
{
// 决策/执行/反馈三张表的 SQLite 存储。与既有 Store 相同：
// 独立连接、WAL、busy_timeout、互斥锁；写入按主键幂等（INSERT OR IGNORE）。
// 延迟打开（ADR-023 稳定外壳）：对象始终构造，构造时不碰数据库；第一次 open() 才打开连接、
// 建表。组合根里只有决策记录器在 affect.enabled 为真时调用 open()，没启用过就没有决策表。
// 打开失败只试一次：之后 open() 返回 false，记录降级为不写（已打 LOG_ERROR）。
// 未打开时各读写方法与打开失败时一样返回失败/空结果，不会自行打开
class DecisionStore
{
public:
    explicit DecisionStore(std::string dbPath);
    ~DecisionStore();

    DecisionStore(const DecisionStore &) = delete;
    DecisionStore &operator=(const DecisionStore &) = delete;

    // 打开连接并建表（幂等、线程安全）；返回是否可用
    bool open();
    // 是否已打开（不触发打开）
    bool isOpen() const;
    bool appendDecision(const DecisionRecord &record, std::string *error = nullptr);
    bool appendExecution(const ExecutionRecord &record, std::string *error = nullptr);
    bool appendFeedback(const FeedbackRecord &record, std::string *error = nullptr);

    // 出站回执回填（R0.2 / B2）：把 queued 推进到 sent/failed/unknown 并写入真实
    // message_id。unknown 只覆盖 queued（不能把已明确的 sent/failed 退化成未知）；
    // sent/failed 覆盖 queued 与 unknown（迟到的明确回执优先）。
    // 返回 true = 有一行被更新；查无此 execution 或状态不允许推进时返回 false
    bool updateExecutionReceipt(const std::string &executionId, const std::string &status,
                                const std::string &outboundMessageId,
                                std::string *error = nullptr);
    // 分段投递的追加 message_id：outbound_message_id 为逗号分隔列表（首段 + 后续段），
    // findExecutionByOutboundMessageId 对列表中任一 ID 都能命中。不改状态
    bool appendExecutionMessageId(const std::string &executionId,
                                  const std::string &outboundMessageId,
                                  std::string *error = nullptr);

    std::optional<DecisionRecord> findDecision(const std::string &decisionId) const;
    std::optional<ExecutionRecord> findExecution(const std::string &executionId) const;

    // 反馈定位（R0.3）：按协议端消息 ID 找执行记录（引用回复她的消息时）；
    // 找不到时按"该群 / 该私聊用户最近一次非 skipped 执行（created_at ≥ since）"兜底。
    // groupId≠0 按群匹配（忽略 userId）；groupId=0 按私聊用户匹配
    std::optional<ExecutionRecord> findExecutionByOutboundMessageId(
        const std::string &outboundMessageId) const;
    std::optional<ExecutionRecord> latestExecution(std::uint64_t groupId, std::uint64_t userId,
                                                   std::int64_t since) const;
    std::vector<DecisionRecord> recentDecisions(std::size_t limit) const;
    std::size_t decisionCount() const;
    std::size_t executionCount(const std::string &decisionId) const;
    std::size_t feedbackCount(const std::string &decisionId) const;
    // 某决策的全部反馈（created_at 升序）；评测/反思与测试读回用
    std::vector<FeedbackRecord> feedbackFor(const std::string &decisionId) const;

private:
    const std::string path;
    sqlite3 *db = nullptr;      // 只在 open() 里由空变非空一次，之后到析构不变
    bool openAttempted = false; // 受 mutex 保护
    mutable std::mutex mutex;
};
} // namespace affect

#endif // AFFECT_DECISION_STORE_H
