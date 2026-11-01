#ifndef AFFECT_DECISION_POLICY_H
#define AFFECT_DECISION_POLICY_H

#include "BehaviorPlan.h"
#include "SituationCard.h"

#include <cstdint>
#include <string>

namespace affect
{
// 一次决策的结果：计划与它的来历一起返回（不经输出参数或线程局部状态），
// 调用方在哪个线程拿到就在哪个线程记录
struct Decision
{
    BehaviorPlan plan;          // 必须 validate() 通过
    std::string fallbackReason; // 成功为空串；成功但有提示时如 allowed_tools_empty
    std::int64_t latencyMs = 0; // 模型调用耗时；未调用模型为 0
};

// 决策点（R1）：两条群回应路径在调用表达模型前都经过同一个策略，得到一份
// BehaviorPlan。BaselinePolicy 复现既有行为；JudgePolicy 由 worker 模型产出计划，
// 任何失败回退 Baseline（= 交给她本人，绝不回退成沉默）。
class DecisionPolicy
{
public:
    virtual ~DecisionPolicy() = default;
    // transientMaterial：内存里的现场原文（只给 judge 看，不进 card、不落库）
    virtual Decision decide(const SituationCard &card, const std::string &transientMaterial) = 0;
    virtual std::string version() const = 0;     // "baseline_v2.4.1" / "judge_v1"
    virtual std::string planVersion() const = 0; // "baseline_plan_v1" / "judge_plan_v1"
};

} // namespace affect

#endif // AFFECT_DECISION_POLICY_H
