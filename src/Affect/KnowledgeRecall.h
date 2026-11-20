#ifndef AFFECT_KNOWLEDGE_RECALL_H
#define AFFECT_KNOWLEDGE_RECALL_H

/*
 * 取用（经历层，ADR-029；设计 §7，先于 E3 实施）：认识账本里的认识怎么进到群聊的每一轮。
 *
 * 取什么：全局的 + 这个群的 + 现场的人的（person 认识的 scope_group 为 0 或本群），只取 active 且
 * 读时算出的有效强度不低于淡出阈值的条目。热路径只读：不跑遗忘、不写库。
 * 挑哪些：教导（作者是 teaching）永远优先——部署者的纠正分量最高；其余按有效强度降序；截到上限。
 * 怎么排：选中的条目最终按编号升序输出。这样强度随时间衰减时文本一个字不变，只有条目进出或被改写
 * 才变——认识段在本轮 user 侧尾部，变得越少越好比对、也越稳。
 * 怎么拼：标题行“[你的认识]”，之后一行一条原文，不带编号、强度、来源。标题是机制文本；正文全部来自
 * 账本（她或部署者写的），可执行文件里没有决定她风格的句子。
 *
 * 开关：只在当次 affect.enabled 快照为真、组合根接了账本时取用（与 factsCardFor 同一判据）；
 * 否则、或没有匹配条目时什么都不加，请求与此前逐字相同（不加空标题）。私聊不取用。
 */
#include "KnowledgeLedger.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace affect
{
// 资源预算（代码常量，不是配置）：判定器看得多一些，表达请求只带最要紧的几条
constexpr std::size_t kJudgeKnowledgeMax = 10;
constexpr std::size_t kReplyKnowledgeMax = 6;

// 这一轮的范围：哪个群、现场有哪些人（材料窗口里的发言者 + 回复候选）
struct KnowledgeScope
{
    std::uint64_t groupId = 0;
    std::vector<std::uint64_t> people;
};

// 纯函数：从取用池里按范围挑出至多 max 条（规则见文件头）。范围之外的（别的群的、不在场的人的）、
// 不是 active 的、有效强度低于 KnowledgeRules::kFadeThreshold 的不取；结果按编号升序
std::vector<KnowledgeEntry> selectKnowledge(const std::vector<RecalledKnowledge> &pool,
                                            const KnowledgeScope &scope, std::size_t max);

// "[你的认识]\n第一条\n第二条"；没有条目时为空串
std::string knowledgeSection(const std::vector<KnowledgeEntry> &chosen);

// 一轮的认识：判定器与表达请求各一份（上限不同），编号写进决策记录的 state_json
struct KnowledgeCard
{
    std::string judgeSection;
    std::vector<std::int64_t> judgeIds;
    std::string replySection;
    std::vector<std::int64_t> replyIds;
};

// 两条群聊路径共用的装配点：affectEnabled 为假或 ledger 为空（未接线）时全空、不碰账本；
// 否则打开账本（与账本工具同一判据，幂等）后读一次取用池，分别按两个上限挑选
KnowledgeCard knowledgeCardFor(bool affectEnabled, KnowledgeLedger *ledger,
                               const KnowledgeScope &scope);
} // namespace affect

#endif // AFFECT_KNOWLEDGE_RECALL_H
