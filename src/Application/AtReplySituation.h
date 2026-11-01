#ifndef AT_REPLY_SITUATION_H
#define AT_REPLY_SITUATION_H

/*
 * @ 回复路径的情境卡装配（R1）：纯函数，Message 无夹具，单测在 AtReplySituationTests。
 * 只放派生字段：topicNote 是观察通道的话题注记（派生），触发句与群背景原文不进 card，
 * 只作为 transientMaterial 在内存里交给 judge。
 */
#include "../Affect/SituationCard.h"
#include "../Port/InboundMessage.h"

#include <string>
#include <utility>
#include <vector>

inline affect::SituationCard atReplySituation(const InboundMessage &message,
                                              const std::string &topicNote,
                                              std::vector<std::string> toolNames, int localHour)
{
    affect::SituationCard card;
    card.source = "at_reply";
    card.triggerKind = "at";
    card.groupId = message.group_id;
    card.userId = message.user_id;
    card.recentSummary = topicNote;
    card.availableTools = std::move(toolNames);
    card.localHour = localHour;
    return card;
}

#endif // AT_REPLY_SITUATION_H
