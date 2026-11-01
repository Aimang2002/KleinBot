#ifndef TOOL_POLICY_FILTER_H
#define TOOL_POLICY_FILTER_H

/*
 * 按计划收窄工具名单（R1，R3.1 提前）：唯一按名字过滤的纯函数。
 * inherit → 原样；allow_list → offered ∩ allowedTools（保持 offered 顺序）；none → 空。
 * 调用方只传注册表工具：会话定案动作（group_say / leave_topic / recall_context）不经过滤，
 * 永远在——tool_policy=none 只意味着不给注册表工具。
 */
#include "../Affect/BehaviorPlan.h"

#include <algorithm>
#include <string>
#include <vector>

inline std::vector<std::string> filterToolNames(const std::vector<std::string> &offered,
                                                const std::string &toolPolicy,
                                                const std::vector<std::string> &allowedTools)
{
    if (toolPolicy == "none")
        return {};
    if (toolPolicy != "allow_list")
        return offered;
    std::vector<std::string> kept;
    for (const std::string &name : offered)
        if (std::find(allowedTools.begin(), allowedTools.end(), name) != allowedTools.end())
            kept.push_back(name);
    return kept;
}

inline std::vector<std::string> filterToolNames(const std::vector<std::string> &offered,
                                                const affect::BehaviorPlan &plan)
{
    return filterToolNames(offered, plan.toolPolicy, plan.allowedTools);
}

#endif // TOOL_POLICY_FILTER_H
