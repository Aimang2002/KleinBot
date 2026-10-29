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
        return textResult("功能未开启");

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
        return textResult("zzz");

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
        return textResult("记不住了");
    }

    // 回复只说一句人话（用户定规 2026-10-29）：匹配方式、时间、备注这些是她的内部事务，
    // 不暴露给群友；排障信息全部进日志
    LOG_INFO("人工反馈：" + label + "（" + feedback.source + "）" +
             (note.empty() ? "" : "，备注=" + note) + "，decision_id=" + hit->decisionId +
             "，对应 " + clockText(hit->createdAt) + " 的回复，matched_by=" + matchedBy +
             (matchedBy == "fallback" && !replyToMessageId.empty() ? "（引用未命中）" : ""));
    return textResult("好的，我记住了。");
}
