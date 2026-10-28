#ifndef RECORD_FEEDBACK_ACTION_H
#define RECORD_FEEDBACK_ACTION_H

#include "Action.h"

#include <cstdint>
#include <functional>

namespace affect
{
class DecisionRecorder;
class DecisionStore;
}

// 部署者人工反馈（R0.3）：对 Klein 的某条回复打 good/bad 标签，写 affect_feedback。
// 定位顺序：引用的消息 ID → affect_executions.outbound_message_id（R0.2 回执回填）；
// 找不到或没引用时，退回该群 / 该私聊最近 10 分钟内的最后一次执行，并在 evidence 里
// 标 matched_by=fallback。只暴露给 #好/#坏 命令，不给模型当工具（expose_as_tool=false）；
// 所有人可用。标注人分两类落在 affect_feedback.source（用户定规 2026-10-28）：
//   manual_admin（bot.manager_id，confidence 1.0）/ manual_user（其他人，confidence 0.6）
// 评测与反思按 source 分组即可区分，不必解析 evidence_json。
// store/recorder 可空（affect 关闭）：返回明确的未启用说明，不抛
class RecordFeedbackAction : public Action
{
public:
    static constexpr std::int64_t kFallbackWindowSeconds = 10 * 60;
    static constexpr double kAdminConfidence = 1.0;
    static constexpr double kUserConfidence = 0.6;

    RecordFeedbackAction(affect::DecisionStore *store, affect::DecisionRecorder *recorder,
                         std::uint64_t managerId, std::function<std::int64_t()> clock = {});

    const ActionDescriptor &descriptor() const override;
    // arguments：label（good|bad，必填）、note（可空）、reply_to_message_id（可空，原样字符串）、
    // group_id（群聊填群号，私聊 0）
    ActionResult execute(const nlohmann::json &arguments, const ActionContext &context) override;

private:
    affect::DecisionStore *const store;
    affect::DecisionRecorder *const recorder;
    const std::uint64_t managerId;
    const std::function<std::int64_t()> clock;
};

#endif // RECORD_FEEDBACK_ACTION_H
