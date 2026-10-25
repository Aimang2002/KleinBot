#ifndef AFFECT_BEHAVIOR_PLAN_H
#define AFFECT_BEHAVIOR_PLAN_H

#include "../../Library/nlohmann/json.hpp"

#include <optional>
#include <string>
#include <vector>

namespace affect
{
// v1 行为计划：M1 只做合同与影子记录，不改变既有决定。
// action 是行为字典的稳定标识；speech_act/tone 在 M2 由策略填充，
// M1 基线记录使用 "unspecified"。max_codepoints=0 表示计划层不额外限制。
// tool_policy: inherit=继承入口既有工具集；allow_list=只允许 allowed_tools；
// none=不提供工具。
struct BehaviorPlan
{
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;
    std::string action;
    std::string speechAct = "unspecified";
    std::string tone = "unspecified";
    int maxCodepoints = 0;
    std::string toolPolicy = "inherit";
    std::vector<std::string> allowedTools;

    bool validate(std::string &error) const;
    nlohmann::json toJson() const;
    static std::optional<BehaviorPlan> fromJson(const nlohmann::json &value,
                                                std::string &error);
};

bool isKnownBehaviorAction(const std::string &action);
bool isKnownToolPolicy(const std::string &toolPolicy);

} // namespace affect

#endif // AFFECT_BEHAVIOR_PLAN_H
