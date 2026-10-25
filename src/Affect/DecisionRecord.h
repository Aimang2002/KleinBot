#ifndef AFFECT_DECISION_RECORD_H
#define AFFECT_DECISION_RECORD_H

#include "BehaviorPlan.h"

#include <cstdint>
#include <string>

namespace affect
{
// 一次决策的统一记录：两条群回应路径（@ 回复、主动接话）都产出这一结构。
// 决策 ID 必须稳定贯穿计划、执行和后续反馈；M1 只记录，不改变既有决定。
struct DecisionRecord
{
    std::string decisionId;
    std::int64_t createdAt = 0;
    std::string source; // at_reply / private_reply / engagement_turn / engagement_cold_entry / engagement_judge
    std::uint64_t groupId = 0;
    std::uint64_t userId = 0;
    std::string triggerMessageId;
    BehaviorPlan plan;
    std::string policyVersion; // 例如 baseline_v2.4.1
    std::string planVersion;   // 编译器/计划格式版本，例如 baseline_plan_v1
    std::string stateJson = "{}"; // 决策时的非原文状态快照；不得写入群聊原文
    std::string fallbackReason;
};

// 执行记录只描述 Klein 自己的输出与投递状态；status 允许
// queued / sent / failed / skipped / unknown / deviated。
struct ExecutionRecord
{
    std::string executionId;
    std::string decisionId;
    std::int64_t createdAt = 0;
    std::string status;
    std::string actualText; // Klein 产生的文本；不复制群聊原文
    std::string outboundMessageId;
    std::string deviationReason;
};

// 反馈记录在 M3 生成；M1 先落合同与表。status 允许 known / unknown / pending。
struct FeedbackRecord
{
    std::string feedbackId;
    std::string decisionId;
    std::int64_t createdAt = 0;
    std::string source; // explicit_reply / mention / manual / none
    std::string status;
    double confidence = 0.0;
    std::string evidenceJson = "{}";
};

// 构造 M1 基线决策：不携带策略推断，只把实际发生的动作纳入统一合同。
// action 使用 BehaviorPlan 的 action 字典；no_reply 自动使用 tool_policy=none。
DecisionRecord makeBaselineDecision(const std::string &source,
                                    std::uint64_t groupId,
                                    std::uint64_t userId,
                                    const std::string &triggerMessageId,
                                    const std::string &action,
                                    const std::string &toolPolicy = "inherit");

} // namespace affect

#endif // AFFECT_DECISION_RECORD_H
