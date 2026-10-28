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
class DecisionStore
{
public:
    explicit DecisionStore(const std::string &dbPath);
    ~DecisionStore();

    DecisionStore(const DecisionStore &) = delete;
    DecisionStore &operator=(const DecisionStore &) = delete;

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

    std::optional<DecisionRecord> findDecision(const std::string &decisionId) const;
    std::optional<ExecutionRecord> findExecution(const std::string &executionId) const;
    std::vector<DecisionRecord> recentDecisions(std::size_t limit) const;
    std::size_t decisionCount() const;
    std::size_t executionCount(const std::string &decisionId) const;
    std::size_t feedbackCount(const std::string &decisionId) const;

private:
    sqlite3 *db = nullptr;
    mutable std::mutex mutex;
};
} // namespace affect

#endif // AFFECT_DECISION_STORE_H
