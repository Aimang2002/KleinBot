#ifndef AFFECT_SITUATION_CARD_H
#define AFFECT_SITUATION_CARD_H

#include "../../Library/nlohmann/json.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace affect
{
// 决策输入（R1）：两条群回应路径在调用表达模型前装配一张，交给 DecisionPolicy。
// 只放派生字段，toJson() 原样写进 DecisionRecord.stateJson（D7）：群消息原文、
// 昵称、requestContext、会话材料一律不得进入。原文只经 DecisionPolicy::decide 的
// transientMaterial 在内存里交给 judge，不落库。
// 会话的 topicLine（激活时=触发句原文）因此不进 card。
struct SituationCard
{
    std::string source;      // at_reply / engagement_turn / engagement_cold_entry（枚举只增不改）
    std::string triggerKind; // at / address / name_mention / follow_up_window / lull / cold_spike
    std::uint64_t groupId = 0;
    std::uint64_t userId = 0;  // @ 路径 = 发送者；会话轮 0（与现有记录口径一致）
    std::string recentSummary; // 会话轮：滚动摘要 digest；@ 路径：话题注记（都是派生文本）
    std::string moodLine;
    int turnCount = 0;
    int consecutivePasses = 0;
    int newMessagesSinceTurn = 0;
    int messagesSeen = 0;
    int followUpTurnsUsed = 0;
    std::int64_t secondsSinceLastTurn = -1;   // -1 = 本会话尚无轮次
    std::int64_t secondsSinceLastSpeech = -1; // -1 = 本会话她尚未开口
    int lullTurnsThisHour = 0;
    int lullHourlyCap = 8;
    int judgeCallsThisHour = 0;
    int judgeCallsCap = 30;
    int localHour = 0;
    bool isQuietHours = false;               // R2 前恒 false
    std::vector<std::string> availableTools; // 名字（含会话动词）
    std::string playbook;                    // R4 前空串
    std::string journalTail;                 // R4 前空串
    std::string planSummary;                 // 对话计划摘要（扩展点）；R1 恒空串

    nlohmann::json toJson() const;
};

} // namespace affect

#endif // AFFECT_SITUATION_CARD_H
