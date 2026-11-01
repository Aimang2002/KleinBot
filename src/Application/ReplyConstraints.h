#ifndef REPLY_CONSTRAINTS_H
#define REPLY_CONSTRAINTS_H

/*
 * 计划对表达模型的约束（R1.c，affect.enforce=true 时）：两条群回应路径共用。
 * 分寸注记的唯一格式化点（planNote）：
 *   "\n\n[系统注] 这一轮的分寸：speech_act=<x>；tone=<y>；最多 <n> 个字符。"
 * 只出现非 unspecified / 非 0 的片段，全无则为空串。长度只是提示，不做硬截断。
 * speech_act / tone 来自 judge 输出，会进 system prompt：含换行或过长（> 16 个字符）的
 * 片段丢弃（安全护栏，正常输出是短词或 2–6 字中文）。
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
