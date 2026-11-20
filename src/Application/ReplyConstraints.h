#ifndef REPLY_CONSTRAINTS_H
#define REPLY_CONSTRAINTS_H

/*
 * 计划对表达模型的约束（R1.c，affect.enforce=true 时）：两条群回应路径共用。
 * 分寸注记的唯一格式化点（planNote）：
 *   "\n\n[系统注] 这一轮的分寸：speech_act=<x>；tone=<y>；最多 <n> 个字符。"
 * 只出现非 unspecified / 非 0 的片段，全无则为空串。长度只是提示，不做硬截断。
 * speech_act / tone 来自 judge 输出，会进 system prompt：含换行或过长（> 16 个字符）的
 * 片段丢弃（安全护栏，正常输出是短词或 2–6 字中文）。
 *
 * 计划 v2（经历层 E1，ADR-027）不再有 speech_act / tone / max_codepoints，分寸注记为空；
 * 取而代之的是：群聊轮不追加服务契约（serviceContract=false）、本轮任务句（taskNote，由调用方
 * 按 reply_to 填，尾插进本轮 user 侧）与结构核对的长度档（lengthTier，非空 = 回复要经
 * affect::checkWithRetry 核对，不合格重说一次）。
 */
#include "../Affect/BehaviorPlan.h"

#include <cstddef>
#include <string>
#include <vector>

struct ReplyConstraints
{
    std::string toolPolicy = "inherit"; // inherit / allow_list / none
    std::vector<std::string> allowedTools;
    int maxCodepoints = 0;              // 0 = 不提示长度
    std::string toneNote;               // "speech_act=<x>；tone=<y>"（片段可缺），可空
    // 计划 v2（经历层 E1）
    bool serviceContract = true; // false = 人格之后不追加服务契约（群聊轮，计划 v2 生效时）
    std::string taskNote;        // 本轮任务句（含前缀），经 appendContextNote 尾插；空 = 不加
    std::string lengthTier;      // short / one / few；空 = 不做结构核对
};

namespace reply_constraints_detail
{
constexpr std::size_t kMaxFragmentCodepoints = 16;

inline bool usableFragment(const std::string &value)
{
    if (value.empty() || value == "unspecified")
        return false;
    if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos)
        return false;
    std::size_t codepoints = 0;
    for (const unsigned char byte : value)
        if ((byte & 0xC0) != 0x80)
            ++codepoints;
    return codepoints <= kMaxFragmentCodepoints;
}
} // namespace reply_constraints_detail

inline ReplyConstraints fromPlan(const affect::BehaviorPlan &plan)
{
    ReplyConstraints constraints;
    constraints.toolPolicy = plan.toolPolicy;
    constraints.allowedTools = plan.allowedTools;
    if (plan.isV2())
    {
        // v2：没有分寸注记；不追加服务契约，回复按长度档核对（计划没给档时用最宽的 few）
        constraints.serviceContract = false;
        constraints.lengthTier = plan.length.empty() ? std::string("few") : plan.length;
        return constraints;
    }
    constraints.maxCodepoints = plan.maxCodepoints;
    if (reply_constraints_detail::usableFragment(plan.speechAct))
        constraints.toneNote = "speech_act=" + plan.speechAct;
    if (reply_constraints_detail::usableFragment(plan.tone))
        constraints.toneNote += (constraints.toneNote.empty() ? "" : "；") + std::string("tone=") +
                                plan.tone;
    return constraints;
}

inline std::string planNote(const ReplyConstraints &constraints)
{
    std::string fragments = constraints.toneNote;
    if (constraints.maxCodepoints > 0)
        fragments += (fragments.empty() ? "" : "；") + std::string("最多 ") +
                     std::to_string(constraints.maxCodepoints) + " 个字符";
    if (fragments.empty())
        return {};
    return "\n\n[系统注] 这一轮的分寸：" + fragments + "。";
}

#endif // REPLY_CONSTRAINTS_H
