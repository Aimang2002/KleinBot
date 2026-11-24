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
// judge 策略（R1；经历层 E1 起产出计划 v2，ADR-027）：worker 模型读她的人格（soul.md）、
// 部署者事实、此刻、SituationCard、可回应的消息与现场材料，只输出一个 JSON 计划（v2）。
// 输出只按 JSON 解析，不做任何前缀/关键词判断；一切失败回退 Baseline（v1 基线计划，她照今天
// 的方式回复）并填 fallbackReason：worker 未接入 judge_unavailable、预算满 judge_budget、
// 该来源/触发不经 judge judge_unsupported_trigger、空串 judge_empty、解析/校验失败
// judge_invalid:<原因>（含不是 v2、occasion 为空）、动作越界 judge_action_not_allowed。
// 成功时 fallbackReason 可能带提示（逗号分隔）：allowed_tools_empty、reply_to_unknown
//（reply_to 不在可回应的消息里，已置空）、occasion_quotes_material / reaction_quotes_material
//（occasion / reaction_to_last 照抄原文，已清空，计划照常生效）。
// 动作空间按触发类型划定（allowedActions，唯一定义处）：被 @ 可以 reply 或 no_reply（她有拒绝的权利，
// 2026-11-22）；有人叫她/接
// 她的话只能 group_say（交给她本人）；只有冷场与冷启动可建议 no_reply / leave_topic。
class JudgePolicy : public DecisionPolicy
{
public:
    using Worker = std::function<std::string(const std::string &systemPrompt,
                                             const std::string &userPrompt)>;
    // 一次决策要调用的模型：调用 seam 与记录用的模型名成对给出，保证调用的模型与记录的
    // 标签一致
    struct Target
    {
        Worker worker;
        std::string modelLabel; // 模型名，不含端点/密钥
    };
    // 每次决策开头调用一次，按当前配置（affect.judge_model、models.worker、注册表）解析
    using TargetResolver = std::function<Target()>;
    // 部署者文件来源（经历层 E0/E1）：返回 source/facts.md、source/soul.md 原文，每次决策调用一次
    using FactsSource = std::function<std::string()>;
    static constexpr int kJudgeCallsPerHour = 30; // 资源护栏：每群每小时（滚动窗口）
    // 隐私护栏：occasion / reaction_to_last 与某条可回应消息的原文连续相同达到这么多码点，
    // 就算照抄原文（这两项要落库，原文不得落库）
    static constexpr std::size_t kQuoteRunCodepoints = 8;
    // judge 调用的 HTTP 超时：卡住时快速失败回退基线（适配器默认 300 秒会把同群
    // 会话轮拖住 5 分钟）。组合根经 makeWorkerSeam 传给 buildOnceWith。
    // 2026-11-24 由 15 秒放宽到 60 秒：线上 102 次调用中位数 8.1 秒、p90 14.1 秒，其中 9 次
    // 撞上 15 秒被掐掉、回退基线——上限切在正常耗时的分布里，而不是只兜卡死的请求。
    // 部署者定为一分钟：模型耗时不由我们控制，从被截断的数据推不出更紧的上限
    static constexpr long kTimeoutMs = 60000;

    // clock 返回 unix 秒（预算窗口用）；空 = 系统时钟。
    // 组合根用 resolver：每次决策按当前配置解析调用目标（ADR-023，配置实时生效）
    explicit JudgePolicy(TargetResolver resolver, std::function<std::int64_t()> clock = {});
    // 固定目标（测试与不需要按配置解析的场景）
    JudgePolicy(Worker worker, std::string modelLabel,
                std::function<std::int64_t()> clock = {});

    // 开头解析一次调用目标，结果的 modelLabel 就是它的模型名（回退也带）；
    // latencyMs 是 worker 调用耗时（未调用 worker 的回退为 0）。decide = 没有可回应消息的 decideFor
    Decision decide(const SituationCard &card, const std::string &transientMaterial) override;
    Decision decideFor(const SituationCard &card, const std::string &transientMaterial,
                       const std::vector<ReplyCandidate> &candidates) override;
    std::string version() const override { return "judge_v2"; }
    std::string planVersion() const override { return "judge_plan_v2"; }

    // 事实卡（经历层 E0）：组合根在启动线程前设置一次。空函数（默认）= 不加事实卡；
    // 设置后每次决策在 prompt 里加“关于她的事实”（来源返回非空时）与“此刻”两段。
    // judge 只在 affect.enabled 为真时被策略外壳选中，事实卡因此也只在那时出现
    void setFactsSource(FactsSource source) { factsSource = std::move(source); }
    // 她的人格（经历层 E1）：组合根接 source/soul.md，每次决策现读；空函数或返回空 = 不加这一段
    void setSoulSource(FactsSource source) { soulSource = std::move(source); }

    // 按当前配置解析出的模型名（不含端点/密钥）；记录里用的是 Decision::modelLabel
    std::string modelLabel() const;
    int callsThisHour(std::uint64_t groupId) const;          // 供 card.judgeCallsThisHour

    static std::string systemPrompt();
    // soul（人格原文）、deployerFacts（部署者事实原文）、sceneFacts（affect::sceneFactsNote 的
    // 一句）、candidates（可回应的消息）非空时各成一段，放在现场材料之前；都为空时只有 card、
    // allowed_actions 与现场材料。card.knowledgeSection（她的认识，ADR-029）非空时接在现场材料之后、
    // 作为 prompt 的最后一段
    static std::string userPrompt(const SituationCard &card, const std::string &material,
                                  const std::string &deployerFacts = {},
                                  const std::string &sceneFacts = {},
                                  const std::string &soul = {},
                                  const std::vector<ReplyCandidate> &candidates = {});
    static std::vector<std::string> allowedActions(const SituationCard &card);
    // 解析 worker 原文为计划 v2：成功返回计划（error 可能带提示）；失败返回空，error 即回退原因
    //（judge_invalid:… / judge_action_not_allowed）。candidates 用于核对 reply_to 与照抄原文
    static std::optional<BehaviorPlan> parse(const std::string &raw, const SituationCard &card,
                                             std::string &error,
                                             const std::vector<ReplyCandidate> &candidates = {});

private:
    bool takeBudget(std::uint64_t groupId);

    TargetResolver resolver;
    std::function<std::int64_t()> clock;
    FactsSource factsSource;
    FactsSource soulSource;
    mutable std::mutex mutex;
    std::map<std::uint64_t, std::deque<std::int64_t>> calls; // 每群调用时刻
};

} // namespace affect

#endif // AFFECT_JUDGE_POLICY_H
