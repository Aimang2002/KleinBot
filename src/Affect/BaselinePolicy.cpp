#include "BaselinePolicy.h"

#include "DecisionRecord.h"

namespace affect
{
BehaviorPlan BaselinePolicy::planFor(const SituationCard &card)
{
    // 会话类来源交给她本人（group_say），其余一律 reply：未知来源也不得变成沉默
    const bool engagement = card.source.rfind("engagement_", 0) == 0;
    return makeBaselineDecision(card.source, card.groupId, card.userId, {},
                                engagement ? "group_say" : "reply")
        .plan;
}

Decision BaselinePolicy::decide(const SituationCard &card, const std::string &)
{
    return Decision{planFor(card), {}, 0};
}
} // namespace affect
