#ifndef SHADOW_DECISION_H
#define SHADOW_DECISION_H

/*
 * @ 回复路径影子期的记录时序（R1 方案 A，唯一实现）：
 *   预分配 decision_id → 执行记录引用它先落库 → 投递 → judge decide → 同一 id 写决策。
 * 为什么执行比决策早：回执规则（R0.2）要求执行行在投递前存在，而影子期不得给 @ 回复
 * 加 judge 的延迟，所以 judge 只能在投递之后跑。judge 失败时策略自带回退（基线计划 +
 * fallback_reason），投递抛异常也照样补写决策后再抛出——不留永久孤儿行；只有进程恰好
 * 在投递与补写之间退出才会剩一条无决策的执行行（导出脚本容忍并计数）。
 */
#include "../Affect/DecisionPolicy.h"
#include "../Affect/DecisionRecord.h"
#include "../Affect/DecisionRecorder.h"

#include <exception>
#include <functional>
#include <string>
#include <utility>

// 返回决策 id（记录器关闭时为空串，此时只投递）
inline std::string recordShadowAroundDelivery(affect::DecisionRecorder &recorder,
                                              affect::DecisionPolicy &policy,
                                              const affect::SituationCard &card,
                                              const std::string &material,
                                              const std::string &triggerMessageId,
                                              affect::ExecutionRecord execution,
                                              const std::function<void()> &deliver)
{
    if (!recorder.enabled())
    {
        deliver();
        return {};
    }
    const std::string decisionId = affect::DecisionRecorder::newDecisionId();
    execution.decisionId = decisionId;
    execution.deviationReason = "shadow";
    recorder.recordExecution(std::move(execution));

    std::exception_ptr failure;
    try
    {
        deliver();
    }
    catch (...)
    {
        failure = std::current_exception();
    }

    const affect::Decision decision = policy.decide(card, material);
    affect::DecisionRecord record = affect::makePolicyDecision(
        card.source, card.groupId, card.userId, triggerMessageId, decision, policy, card);
    record.decisionId = decisionId;
    recorder.recordDecision(std::move(record));
    if (failure)
        std::rethrow_exception(failure);
    return decisionId;
}

#endif // SHADOW_DECISION_H
