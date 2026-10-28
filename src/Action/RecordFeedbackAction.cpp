#include "RecordFeedbackAction.h"

#include "../Affect/DecisionRecord.h"
#include "../Affect/DecisionRecorder.h"
#include "../Affect/DecisionStore.h"
#include "../Log/Log.h"

#include <cstdio>
#include <ctime>
#include <optional>
#include <string>

namespace
{
std::string clockText(std::int64_t ts)
{
    // 与 EngagementService::formatClock 同一写法（std::localtime，两平台通用）
    const std::time_t seconds = static_cast<std::time_t>(ts);
    const std::tm *local = std::localtime(&seconds);
    if (local == nullptr)
        return "??:??";
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", local->tm_hour, local->tm_min);
    return buffer;
}

ActionResult textResult(std::string text)
{
    return {std::move(text), {}, {}, true};
}
} // namespace

RecordFeedbackAction::RecordFeedbackAction(affect::DecisionStore *store,
                                           affect::DecisionRecorder *recorder,
                                           std::uint64_t managerId,
                                           std::function<std::int64_t()> clock)
    : store(store), recorder(recorder), managerId(managerId),
      clock(clock ? std::move(clock)
                  : [] { return static_cast<std::int64_t>(std::time(nullptr)); })
{
}

const ActionDescriptor &RecordFeedbackAction::descriptor() const
{
    static const ActionDescriptor value{
        "record_feedback",
        "用户对 Klein 某条回复的人工反馈（good/bad），写入心境反馈表；仅命令入口，不给模型调用",
        {{"type", "object"},
         {"properties",
          {{"label", {{"type", "string"}, {"enum", {"good", "bad"}}}},
           {"note", {{"type", "string"}}},
           {"reply_to_message_id", {{"type", "string"}}},
           {"group_id", {{"type", "integer"}}}}},
         {"required", {"label"}}},
        false,
        false};
    return value;
}

ActionResult RecordFeedbackAction::execute(const nlohmann::json &arguments,
                                           const ActionContext &context)
{
    if (store == nullptr || recorder == nullptr || !recorder->enabled())
        return textResult("心境决策记录未启用（affect.enabled=false），无法记录反馈。");

    const std::string label = arguments.value("label", std::string());
    if (label != "good" && label != "bad")
        return textResult("错误：label 必须是 good 或 bad。");
    const std::string note = arguments.value("note", std::string());
    const std::string replyToMessageId = arguments.value("reply_to_message_id", std::string());
    const std::uint64_t groupId = arguments.value("group_id", static_cast<std::uint64_t>(0));

    std::optional<affect::ExecutionRecord> hit;
    std::string matchedBy;
    if (!replyToMessageId.empty())
    {
        hit = store->findExecutionByOutboundMessageId(replyToMessageId);
        if (hit)
            matchedBy = "message_id";
    }
    if (!hit)
    {
        hit = store->latestExecution(groupId, context.user_id, clock() - kFallbackWindowSeconds);
        if (hit)
            matchedBy = "fallback";
    }
    if (!hit)
        return textResult("最近 10 分钟内没有可标注的回复；请引用她的那条消息再发一次。");

    // 标注人分类：管理员 / 网友——落在 source 列，评测按列分组即可
    const bool isAdmin = managerId != 0 && context.user_id == managerId;

    nlohmann::json evidence;
    evidence["label"] = label;
    evidence["note"] = note;
    evidence["by"] = context.user_id;
    evidence["rater"] = isAdmin ? "admin" : "user";
    evidence["matched_by"] = matchedBy;
    evidence["execution_id"] = hit->executionId;
    evidence["outbound_message_id"] = hit->outboundMessageId;
    if (!replyToMessageId.empty())
        evidence["reply_to_message_id"] = replyToMessageId;

    affect::FeedbackRecord feedback;
    feedback.decisionId = hit->decisionId;
    feedback.source = isAdmin ? "manual_admin" : "manual_user";
    feedback.status = "known";
    feedback.confidence = isAdmin ? kAdminConfidence : kUserConfidence;
    feedback.evidenceJson = evidence.dump();
    if (!recorder->recordFeedback(feedback))
    {
        LOG_ERROR("人工反馈写入失败：decision_id=" + hit->decisionId);
        return textResult("错误：反馈写入失败，请查看日志。");
    }

    std::string reply = std::string("记下了：") + (label == "good" ? "好" : "坏");
    if (!note.empty())
        reply += "（" + note + "）";
    reply += "，对应她 " + clockText(hit->createdAt) + " 的回复";
    if (matchedBy == "fallback")
        reply += replyToMessageId.empty() ? "（未引用消息，按最近一次回复匹配）"
                                          : "（引用的消息未匹配到记录，按最近一次回复匹配）";
    reply += "。";
    LOG_INFO("人工反馈：" + label + "（" + feedback.source + "），decision_id=" + hit->decisionId +
             "，matched_by=" + matchedBy);
    return textResult(std::move(reply));
}
