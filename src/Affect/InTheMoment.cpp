#include "InTheMoment.h"

#include "../utils/Utils.hpp"

namespace affect
{
std::optional<FeedbackRecord> inTheMomentFeedback(const std::string &previousDecisionId,
                                                  const std::string &currentDecisionId,
                                                  const BehaviorPlan &plan)
{
    if (previousDecisionId.empty() || previousDecisionId == currentDecisionId || !plan.isV2())
        return std::nullopt;
    const std::string reading = utils::trim(plan.reactionToLast);
    if (reading.empty())
        return std::nullopt;

    FeedbackRecord feedback;
    feedback.decisionId = previousDecisionId;
    feedback.source = "in_the_moment";
    feedback.status = "known";
    feedback.confidence = 0.0;
    feedback.evidenceJson =
        nlohmann::json{{"reading", reading}, {"observed_in", currentDecisionId}}.dump();
    return feedback;
}
} // namespace affect
