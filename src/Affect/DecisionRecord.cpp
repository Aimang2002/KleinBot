#include "DecisionRecord.h"

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
} // namespace affect
