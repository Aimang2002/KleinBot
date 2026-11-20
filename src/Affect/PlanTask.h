#ifndef AFFECT_PLAN_TASK_H
#define AFFECT_PLAN_TASK_H

/*
 * 计划 v2 到表达模型的本轮任务（经历层 E1“再说”，ADR-027）。
 * 表达模型只负责措辞：她读到人格、事实、现场材料，以及这里用中文写清楚的一句任务，例如
 *     回应老余的那句「普鸭的挺好听的」；这一句是随口接话；一句以内。
 * 这里只是把计划的封闭取值（move、length）和她回应的那条消息翻成中文，不加任何说话风格的
 * 要求；说话的味道来自人格与她自己的认识。任务句每轮都变，只进本轮 user 侧（请求副本，不落库）。
 */
#include "BehaviorPlan.h"
#include "DecisionPolicy.h"

#include <optional>
#include <string>
#include <vector>

namespace affect
{
// move 的中文：answer 回答、follow 随口接话、agree 附和、comfort 安慰、tease 调侃、ask 提问、
// ack 应一声；未知为空
std::string moveLabel(const std::string &move);
// length 的中文：short 一短句、one 一句以内、few 两三句以内；未知为空
std::string lengthLabel(const std::string &length);

// 在可回应的消息里按 reply_to 找那一条；reply_to 为空或不在其中返回空指针
const ReplyCandidate *findCandidate(const std::vector<ReplyCandidate> &candidates,
                                    const std::optional<std::string> &replyTo);

// 本轮任务句（不带前缀）。v1 计划、no_reply 返回空串。
//   说话类：[回应<昵称>的那句「…」；]这一句是<做法>；<长度>。
//   离场：这一轮打算离场，用 leave_topic[；要留最后一句的话，<长度>]。
std::string planTaskSentence(const BehaviorPlan &plan, const ReplyCandidate *target);

} // namespace affect

#endif // AFFECT_PLAN_TASK_H
