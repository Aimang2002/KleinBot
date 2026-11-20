#ifndef KNOWLEDGE_ACTION_H
#define KNOWLEDGE_ACTION_H

#include "Action.h"
#include "../Affect/AffectOptions.h"
#include "../utils/Live.h"

#include <cstdint>
#include <string>

namespace affect
{
class KnowledgeLedger;
}

// 认识账本的三个工具（经历层 E2，ADR-028；设计 §6.1“被人教”、§6.3、§6.4）：
// remember_guidance 记下一句（origin=teaching，经账本的代码校验）、review_guidance 翻看、
// forget_guidance 忘掉。部署者不再用 # 命令教她：他在私聊里说话，记不记、记成什么由模型自己判断，
// 代码只按规矩记账。
// - 只在管理员私聊里开放（managerPrivateOnly，经 ActionTool::offeredIn 转出）：群里、别人的私聊里
//   工具表与使用规则都没有它们；requires_admin 再挡一道（工具循环按调用者核对）。
// - affect.enabled 当前为假时不可用（available），工具表里不出现，账本不打开（没启用过就没有账本表）。
// - 每次调用（按范围忘掉除外）开头先跑一次遗忘：账本没有自己的定时器（E3 的回想调度接上后也会跑）。
// - 回滚不对外：KnowledgeLedger 的实现与历史表留给 E3 与排障。
// - 没有使用规则（usageGuidance 为空）：描述只讲机制，不写她该怎么做人。
// 回复是给模型看的短文本，排障信息进日志；日志里没有认识正文
class KnowledgeAction : public Action
{
public:
    static constexpr std::size_t kMaxListedEntries = 30;
    static constexpr std::size_t kMaxHistoryRows = 10;

    bool available() const override;
    bool managerPrivateOnly() const override { return true; }
    ActionResult execute(const nlohmann::json &arguments, const ActionContext &context) override;

protected:
    // ledger、options 由组合根持有、须比本对象活得久
    KnowledgeAction(affect::KnowledgeLedger &ledger, const Live<AffectOptions> &options);

    // 账本已打开、遗忘已跑过（decayFirst 为真时）之后的具体操作
    virtual ActionResult run(const nlohmann::json &arguments, const ActionContext &context) = 0;
    // 开头要不要先跑一次遗忘（按范围忘掉时不跑：要删的东西不必先记成淡了）
    virtual bool decayFirst(const nlohmann::json &) const { return true; }

    affect::KnowledgeLedger &ledger;
    const Live<AffectOptions> &options;
};

// 记下一句：text（≤ 80 码点、一行）、scope（global | group:<群号> | user:<QQ号>，默认 global）、
// 可选 replaces（编号，改写那一条）
class RememberGuidanceAction : public KnowledgeAction
{
public:
    RememberGuidanceAction(affect::KnowledgeLedger &ledger, const Live<AffectOptions> &options);
    const ActionDescriptor &descriptor() const override;

private:
    ActionResult run(const nlohmann::json &arguments, const ActionContext &context) override;
};

// 翻看：可选 scope（缺省 = 全部范围，至多 kMaxListedEntries 行）、可选 id（某一条及其变化）
class ReviewGuidanceAction : public KnowledgeAction
{
public:
    ReviewGuidanceAction(affect::KnowledgeLedger &ledger, const Live<AffectOptions> &options);
    const ActionDescriptor &descriptor() const override;

private:
    ActionResult run(const nlohmann::json &arguments, const ActionContext &context) override;
};

// 忘掉：id（撤回那一条，历史保留）或 scope（group:<群号> / user:<QQ号>，连同历史删除）
class ForgetGuidanceAction : public KnowledgeAction
{
public:
    ForgetGuidanceAction(affect::KnowledgeLedger &ledger, const Live<AffectOptions> &options);
    const ActionDescriptor &descriptor() const override;

private:
    ActionResult run(const nlohmann::json &arguments, const ActionContext &context) override;
    bool decayFirst(const nlohmann::json &arguments) const override;
};

#endif // KNOWLEDGE_ACTION_H
