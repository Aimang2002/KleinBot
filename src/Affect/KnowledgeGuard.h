#ifndef AFFECT_KNOWLEDGE_GUARD_H
#define AFFECT_KNOWLEDGE_GUARD_H

/*
 * 认识账本的代码校验（经历层 E2，ADR-028；设计 §6.2 “代码校验”）。安全护栏，不判断好坏：
 * - 结构：op、kind 在封闭集合里；key 非空、≤ 64 字节、不含空白；
 * - 长度：正文非空、一行、≤ 80 码点；检索词一行、≤ 120 码点；
 * - 作用域：group 只能是本群；person 的 scope_user 非 0，回想时必须是本次材料里出现过的人；
 *   habit 为全局（scope_group = 0）或本群；person 的 scope_group 同样只能是 0 或本群；
 * - 依据：回想至少带一条；每条形如 decision:<id> / feedback:<id> 且必须存在；
 * - 隐私：正文、检索词与本次材料任一行连续 8 个码点相同即算照抄，不收。
 * 每次至多 8 条改动、教导过的认识回想不能动，这两条依赖账本状态，在 KnowledgeLedger::apply 里查。
 */
#include "KnowledgeLedger.h"

#include <string>
#include <vector>

namespace affect
{
class DecisionStore;

class KnowledgeGuard
{
public:
    static constexpr std::size_t kQuoteRunCodepoints = 8;

    // 单条改动的结构、长度、作用域、依据、照抄检查；不合格时 reason 写一句中文原因
    static bool check(const KnowledgeChange &change, const KnowledgeRunContext &context,
                      std::string &reason);
    // 作用域是否合法（按编号改动已有条目时，ledger 对那一条的实际作用域再查一次）
    static bool scopeLegal(const std::string &kind, std::uint64_t scopeGroup,
                           std::uint64_t scopeUser, const KnowledgeRunContext &context,
                           std::string &reason);
    // text 与 material 任一行连续 kQuoteRunCodepoints 个码点相同
    static bool quotesMaterial(const std::string &text, const std::vector<std::string> &material);
};

// 依据核对器：decision:<id> 查决策表，feedback:<id> 查反馈表（store 须比返回值活得久）
std::function<bool(const std::string &)> evidenceCheckerFor(const DecisionStore &store);
} // namespace affect

#endif // AFFECT_KNOWLEDGE_GUARD_H
