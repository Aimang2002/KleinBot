#ifndef AFFECT_BASELINE_POLICY_H
#define AFFECT_BASELINE_POLICY_H

#include "DecisionPolicy.h"

namespace affect
{
// 基线策略：复现 v2.4.1 的决策口径（与 makeBaselineDecision 同一份计划）。
// @ / 私聊 → reply/inherit；会话轮与冷启动入场 → group_say/inherit，即"把决定交给她本人"
// （她仍可在会话轮里沉默/离场）。也是 JudgePolicy 一切失败的回退计划来源。
class BaselinePolicy : public DecisionPolicy
{
public:
    Decision decide(const SituationCard &card, const std::string &transientMaterial) override;
    std::string version() const override { return "baseline_v2.4.1"; }
    std::string planVersion() const override { return "baseline_plan_v1"; }

    static BehaviorPlan planFor(const SituationCard &card);
};

} // namespace affect

#endif // AFFECT_BASELINE_POLICY_H
