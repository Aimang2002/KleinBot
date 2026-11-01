#include "SituationCard.h"

namespace affect
{
nlohmann::json SituationCard::toJson() const
{
    return nlohmann::json{
        {"source", source},
        {"trigger_kind", triggerKind},
        {"group_id", groupId},
        {"user_id", userId},
        {"recent_summary", recentSummary},
        {"mood_line", moodLine},
        {"turn_count", turnCount},
        {"consecutive_passes", consecutivePasses},
        {"new_messages_since_turn", newMessagesSinceTurn},
        {"messages_seen", messagesSeen},
        {"follow_up_turns_used", followUpTurnsUsed},
        {"seconds_since_last_turn", secondsSinceLastTurn},
        {"seconds_since_last_speech", secondsSinceLastSpeech},
        {"lull_turns_this_hour", lullTurnsThisHour},
        {"lull_hourly_cap", lullHourlyCap},
        {"judge_calls_this_hour", judgeCallsThisHour},
        {"judge_calls_cap", judgeCallsCap},
        {"local_hour", localHour},
        {"is_quiet_hours", isQuietHours},
        {"available_tools", availableTools},
        {"playbook", playbook},
        {"journal_tail", journalTail},
        {"plan_summary", planSummary}};
}
} // namespace affect
