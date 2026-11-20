#include "PlanTask.h"

namespace affect
{
std::string moveLabel(const std::string &move)
{
    if (move == "answer")
        return "回答";
    if (move == "follow")
        return "随口接话";
    if (move == "agree")
        return "附和";
    if (move == "comfort")
        return "安慰";
    if (move == "tease")
        return "调侃";
    if (move == "ask")
        return "提问";
    if (move == "ack")
        return "应一声";
    return {};
}

std::string lengthLabel(const std::string &length)
{
    if (length == "short")
        return "一短句";
    if (length == "one")
        return "一句以内";
    if (length == "few")
        return "两三句以内";
    return {};
}

const ReplyCandidate *findCandidate(const std::vector<ReplyCandidate> &candidates,
                                    const std::optional<std::string> &replyTo)
{
    if (!replyTo)
        return nullptr;
    for (const ReplyCandidate &candidate : candidates)
        if (candidate.messageId == *replyTo)
            return &candidate;
    return nullptr;
}

std::string planTaskSentence(const BehaviorPlan &plan, const ReplyCandidate *target)
{
    if (!plan.isV2())
        return {};
    const std::string length = lengthLabel(plan.length);
    if (plan.action == "leave_topic")
        return "这一轮打算离场，用 leave_topic" +
               (length.empty() ? std::string() : "；要留最后一句的话，" + length) + "。";
    if (!isSpeakingAction(plan.action))
        return {};

    std::string sentence;
    if (target != nullptr && !target->quote.empty())
    {
        const std::string speaker = target->speaker.empty() ? std::string("对方") : target->speaker;
        // 引语是「…」时是“那句”，是短词（一张图片）时是“发的”
        sentence = "回应" + speaker +
                   (target->quote.rfind("「", 0) == 0 ? "的那句" : "发的") + target->quote + "；";
    }
    const std::string move = moveLabel(plan.move);
    if (!move.empty())
        sentence += "这一句是" + move + "；";
    sentence += length.empty() ? std::string() : length;
    // 去掉可能多出的结尾分号
    const std::string semicolon = "；";
    if (sentence.size() >= semicolon.size() &&
        sentence.compare(sentence.size() - semicolon.size(), semicolon.size(), semicolon) == 0)
        sentence.erase(sentence.size() - semicolon.size());
    return sentence.empty() ? sentence : sentence + "。";
}

} // namespace affect
