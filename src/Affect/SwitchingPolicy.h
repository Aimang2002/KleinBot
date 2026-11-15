#ifndef AFFECT_SWITCHING_POLICY_H
#define AFFECT_SWITCHING_POLICY_H

#include "AffectOptions.h"
#include "DecisionPolicy.h"
#include "../utils/Live.h"

namespace affect
{
// 决策策略的稳定外壳（ADR-023，配置实时生效）：组合根里始终存在的唯一策略，两条群回应路径
// 都拿它。affect.enabled 为真且 affect.policy 为 judge 时转给 JudgePolicy，否则转给
// BaselinePolicy。两个被转给的策略都由组合根持有、始终构造，judge 的每群小时预算因此
// 跨切换保留。调用方经 resolve(快照) 一次操作只选一次（见 DecisionPolicy::resolve），
// 之后的判断、decide 与记录都用选出的那一个；直接调本对象的 decide 等同于按当前配置选一次
class SwitchingPolicy : public DecisionPolicy
{
public:
    // 三个引用都须比本对象活得久
    SwitchingPolicy(const Live<AffectOptions> &options, DecisionPolicy &baseline,
                    DecisionPolicy &judge)
        : options(options), baseline(baseline), judge(judge)
    {
    }

    Decision decide(const SituationCard &card, const std::string &transientMaterial) override
    {
        return current().decide(card, transientMaterial);
    }
    std::string version() const override { return current().version(); }
    std::string planVersion() const override { return current().planVersion(); }

    DecisionPolicy &resolve(const AffectOptions &snapshot) override { return select(snapshot); }

private:
    DecisionPolicy &select(const AffectOptions &snapshot) const
    {
        return snapshot.enabled && snapshot.policy == "judge" ? judge : baseline;
    }
    DecisionPolicy &current() const { return select(*options.get()); }

    const Live<AffectOptions> &options;
    DecisionPolicy &baseline;
    DecisionPolicy &judge;
};

} // namespace affect

#endif // AFFECT_SWITCHING_POLICY_H
