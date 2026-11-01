#ifndef AFFECT_JUDGE_POLICY_H
#define AFFECT_JUDGE_POLICY_H

#include "DecisionPolicy.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace affect
{
// judge 策略（R1）：worker 模型读 SituationCard + 现场材料，只输出一个 JSON 计划。
// 输出只按 JSON 解析，不做任何前缀/关键词判断；一切失败回退 Baseline 并填
// fallbackReason：worker 未接入 judge_unavailable、预算满 judge_budget、该来源/触发
// 不经 judge judge_unsupported_trigger、空串 judge_empty、解析/校验失败
// judge_invalid:<原因>、动作越界 judge_action_not_allowed。
// 动作空间按触发类型划定（allowedActions，唯一定义处）：被 @ 只能 reply；有人叫她/接
// 她的话只能 group_say（交给她本人）；只有冷场与冷启动可建议 no_reply / leave_topic。
class JudgePolicy : public DecisionPolicy
{
public:
    using Worker = std::function<std::string(const std::string &systemPrompt,
                                             const std::string &userPrompt)>;
    static constexpr int kJudgeCallsPerHour = 30; // 资源护栏：每群每小时（滚动窗口）
    static constexpr int kMaxCodepoints = 400;    // 计划长度上限的夹紧值
    // judge 调用的 HTTP 超时：卡住时快速失败回退基线（适配器默认 300 秒会把同群
    // 会话轮拖住 5 分钟）。组合根经 makeWorkerSeam 传给 buildOnceWith
    static constexpr long kTimeoutMs = 15000;

    // clock 返回 unix 秒（预算窗口用）；空 = 系统时钟
    JudgePolicy(Worker worker, std::string modelLabel,
                std::function<std::int64_t()> clock = {});

    // 结果的 latencyMs 是 worker 调用耗时（未调用 worker 的回退为 0）
    Decision decide(const SituationCard &card, const std::string &transientMaterial) override;
    std::string version() const override { return "judge_v1"; }
    std::string planVersion() const override { return "judge_plan_v1"; }

    const std::string &modelLabel() const { return label; } // 模型名，不含端点/密钥
    int callsThisHour(std::uint64_t groupId) const;          // 供 card.judgeCallsThisHour

    static std::string systemPrompt();
    static std::string userPrompt(const SituationCard &card, const std::string &material);
    static std::vector<std::string> allowedActions(const SituationCard &card);
    // 解析 worker 原文：成功返回计划（error 可能带提示，如 allowed_tools_empty）；
    // 失败返回空，error 即回退原因（judge_invalid:… / judge_action_not_allowed）
    static std::optional<BehaviorPlan> parse(const std::string &raw, const SituationCard &card,
                                             std::string &error);

private:
    bool takeBudget(std::uint64_t groupId);

    Worker worker;
    std::string label;
    std::function<std::int64_t()> clock;
    mutable std::mutex mutex;
    std::map<std::uint64_t, std::deque<std::int64_t>> calls; // 每群调用时刻
};

} // namespace affect

#endif // AFFECT_JUDGE_POLICY_H
