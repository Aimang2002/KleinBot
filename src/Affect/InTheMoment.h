#ifndef AFFECT_IN_THE_MOMENT_H
#define AFFECT_IN_THE_MOMENT_H

/*
 * 当场察觉（经历层 E2，ADR-028；设计 §6.1 第一行）：同一会话里她的下一轮，判定器本来就看得到
 * 她上一句之后的现场，计划 v2 的 reaction_to_last 写的就是她对那之后反应的读法。这里把它记成
 * 上一次决策的一条“后来怎样”——affect_feedback（source=in_the_moment），挂到她说那句话的决策上。
 * 不增加任何模型调用（设计 §14），只是把判定器已经写下的派生文字归到对的经历上。
 *
 * 只收派生文字：reaction_to_last 在 JudgePolicy 里已过照抄护栏（与可回应消息原文连续 8 码点
 * 相同即清空），这里不再碰群聊原文。代码不打分（设计 §6.1“什么算好、什么算坏，不由代码打分”）：
 * status=known、confidence=0，好坏只在她的读法里。
 */
#include "BehaviorPlan.h"
#include "DecisionRecord.h"

#include <optional>
#include <string>

namespace affect
{
// 上一句的决策 id 为空、本轮计划不是 v2 或没写 reaction_to_last 时返回空；
// 否则返回一条待记录的反馈（feedback_id、created_at 由记录器填）。
// evidence_json：{"reading": 她的读法, "observed_in": 本轮决策 id}
std::optional<FeedbackRecord> inTheMomentFeedback(const std::string &previousDecisionId,
                                                  const std::string &currentDecisionId,
                                                  const BehaviorPlan &plan);
} // namespace affect

#endif // AFFECT_IN_THE_MOMENT_H
