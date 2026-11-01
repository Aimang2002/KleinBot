#include "DecisionRecord.h"

#include "JudgePolicy.h"

namespace affect
{
DecisionRecord makeBaselineDecision(const std::string &source,
                                    std::uint64_t groupId,
                                    std::uint64_t userId,
                                    const std::string &triggerMessageId,
                                    const std::string &action,
                                    const std::string &toolPolicy)
{
    DecisionRecord record;
    record.source = source;
    record.groupId = groupId;
    record.userId = userId;
    record.triggerMessageId = triggerMessageId;
    record.plan.action = action;
    record.plan.speechAct = "unspecified";
    record.plan.tone = "unspecified";
    record.plan.maxCodepoints = 0;
    record.plan.toolPolicy = action == "no_reply" ? "none" : toolPolicy;
    record.policyVersion = "baseline_v2.4.1";
    record.planVersion = "baseline_plan_v1";
    record.stateJson = "{}";
    return record;
}

DecisionRecord makePolicyDecision(const std::string &source,
                                  std::uint64_t groupId,
                                  std::uint64_t userId,
                                  const std::string &triggerMessageId,
                                  const Decision &decision,
                                  const DecisionPolicy &policy,
                                  const SituationCard &card)
{
    DecisionRecord record;
    record.source = source;
    record.groupId = groupId;
    record.userId = userId;
    record.triggerMessageId = triggerMessageId;
    record.plan = decision.plan;
    record.policyVersion = policy.version();
    record.planVersion = policy.planVersion();
    record.fallbackReason = decision.fallbackReason;
    nlohmann::json state = card.toJson();
    if (const auto *judge = dynamic_cast<const JudgePolicy *>(&policy))
    {
        state["judge_latency_ms"] = decision.latencyMs;
        state["judge_model"] = judge->modelLabel();
    }
    record.stateJson = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    return record;
}
} // namespace affect
