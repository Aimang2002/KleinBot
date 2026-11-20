#include "KnowledgeRecall.h"

#include <algorithm>
#include <set>

namespace affect
{
namespace
{
bool inScope(const KnowledgeEntry &entry, const KnowledgeScope &scope,
             const std::set<std::uint64_t> &present)
{
    const bool globalOrHere =
        entry.scopeGroup == 0 || (scope.groupId != 0 && entry.scopeGroup == scope.groupId);
    if (entry.kind == "habit")
        return globalOrHere;
    if (entry.kind == "group")
        return scope.groupId != 0 && entry.scopeGroup == scope.groupId;
    if (entry.kind == "person")
        return globalOrHere && entry.scopeUser != 0 && present.count(entry.scopeUser) > 0;
    return false; // 不认识的种类不取（账本只写这三种，这里不猜）
}
} // namespace

std::vector<KnowledgeEntry> selectKnowledge(const std::vector<RecalledKnowledge> &pool,
                                            const KnowledgeScope &scope, std::size_t max)
{
    const std::set<std::uint64_t> present(scope.people.begin(), scope.people.end());
    std::vector<const RecalledKnowledge *> kept;
    for (const RecalledKnowledge &item : pool)
    {
        if (item.entry.status != "active" || item.entry.text.empty())
            continue;
        if (item.effective < KnowledgeRules::kFadeThreshold)
            continue;
        if (!inScope(item.entry, scope, present))
            continue;
        kept.push_back(&item);
    }
    // 教导优先，其余按有效强度降序；同强度按编号，保证截断结果确定
    std::sort(kept.begin(), kept.end(), [](const RecalledKnowledge *a, const RecalledKnowledge *b)
              {
                  if (a->teaching != b->teaching)
                      return a->teaching;
                  if (a->effective != b->effective)
                      return a->effective > b->effective;
                  return a->entry.id < b->entry.id;
              });
    if (kept.size() > max)
        kept.resize(max);
    // 输出按编号升序：强度衰减只影响谁被截掉，不影响留下的顺序
    std::sort(kept.begin(), kept.end(), [](const RecalledKnowledge *a, const RecalledKnowledge *b)
              { return a->entry.id < b->entry.id; });
    std::vector<KnowledgeEntry> chosen;
    chosen.reserve(kept.size());
    for (const RecalledKnowledge *item : kept)
        chosen.push_back(item->entry);
    return chosen;
}

std::string knowledgeSection(const std::vector<KnowledgeEntry> &chosen)
{
    if (chosen.empty())
        return {};
    std::string section = "[你的认识]";
    for (const KnowledgeEntry &entry : chosen)
        section += "\n" + entry.text;
    return section;
}

namespace
{
std::vector<std::int64_t> idsOf(const std::vector<KnowledgeEntry> &chosen)
{
    std::vector<std::int64_t> ids;
    ids.reserve(chosen.size());
    for (const KnowledgeEntry &entry : chosen)
        ids.push_back(entry.id);
    return ids;
}
} // namespace

KnowledgeCard knowledgeCardFor(bool affectEnabled, KnowledgeLedger *ledger,
                               const KnowledgeScope &scope)
{
    KnowledgeCard card;
    if (!affectEnabled || ledger == nullptr)
        return card;
    // 重启后账本要等第一次用到才打开：这里与账本工具同一判据（affect 开着）打开，否则重启后
    // 取用会一直是空的。open() 幂等、失败只试一次
    if (!ledger->open())
        return card;
    const std::vector<RecalledKnowledge> pool = ledger->recallPool(scope.groupId);
    if (pool.empty())
        return card;
    const std::vector<KnowledgeEntry> judge = selectKnowledge(pool, scope, kJudgeKnowledgeMax);
    const std::vector<KnowledgeEntry> reply = selectKnowledge(pool, scope, kReplyKnowledgeMax);
    card.judgeSection = knowledgeSection(judge);
    card.judgeIds = idsOf(judge);
    card.replySection = knowledgeSection(reply);
    card.replyIds = idsOf(reply);
    return card;
}
} // namespace affect
