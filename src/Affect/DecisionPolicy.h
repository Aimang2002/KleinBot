#ifndef AFFECT_DECISION_POLICY_H
#define AFFECT_DECISION_POLICY_H

#include "AffectOptions.h"
#include "BehaviorPlan.h"
#include "SituationCard.h"

#include <cstdint>
#include <string>
#include <vector>

namespace affect
{
// 一次决策的结果：计划与它的来历一起返回（不经输出参数或线程局部状态），
// 调用方在哪个线程拿到就在哪个线程记录
struct Decision
{
    BehaviorPlan plan;          // 必须 validate() 通过
    std::string fallbackReason; // 成功为空串；成功但有提示时如 allowed_tools_empty
    std::int64_t latencyMs = 0; // 模型调用耗时；未调用模型为 0
    // judge 本次决策按当前配置解析出的模型名（与实际调用的模型成对；记录进 state 的
    // judge_model）；基线为空串。只是模型名，不含端点/密钥
    std::string modelLabel;
};

// 这一轮她可以回应的一条群消息（经历层 E1）：计划 v2 的 reply_to 只能从这里选。
// 只在内存里交给 judge 与表达模型，不进 card、不落库（text、quote 都是群友原文）
struct ReplyCandidate
{
    std::string messageId; // 原始 message_id（数字或字符串形态）
    std::string speaker;   // 昵称快照
    std::string quote;     // 「前 12 码点…」或非文字内容的短词（一张图片），同群聊材料的引语
    std::string text;      // 原文（judge 核对 occasion 有没有照抄原文用）
};

// 决策点（R1）：两条群回应路径在调用表达模型前都经过同一个策略，得到一份
// BehaviorPlan。BaselinePolicy 复现既有行为；JudgePolicy 由 worker 模型产出计划，
// 任何失败回退 Baseline（= 交给她本人，绝不回退成沉默）。
class DecisionPolicy
{
public:
    virtual ~DecisionPolicy() = default;
    // transientMaterial：内存里的现场原文（只给 judge 看，不进 card、不落库）
    virtual Decision decide(const SituationCard &card, const std::string &transientMaterial) = 0;
    // 带可回应消息的决策（经历层 E1）：judge 据此给 reply_to；不关心的策略（基线）忽略，
    // 默认就是 decide
    virtual Decision decideFor(const SituationCard &card, const std::string &transientMaterial,
                               const std::vector<ReplyCandidate> &)
    {
        return decide(card, transientMaterial);
    }
    virtual std::string version() const = 0;     // "baseline_v2.4.1" / "judge_v2"
    virtual std::string planVersion() const = 0; // "baseline_plan_v1" / "judge_plan_v2"

    // 按给定的配置快照选出这一次实际生效的策略。普通策略就是自己；稳定外壳
    // （SwitchingPolicy）按 affect.enabled / affect.policy 选基线或 judge。
    // 调用方一次操作只解析一次，之后的判断、decide、记录都用返回的那一个
    virtual DecisionPolicy &resolve(const AffectOptions &) { return *this; }
};

} // namespace affect

#endif // AFFECT_DECISION_POLICY_H
