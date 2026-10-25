#include "BehaviorPlan.h"

#include <cstdint>
#include <set>

namespace affect
{
namespace
{
constexpr const char *kKnownActions[] = {"reply", "group_say", "no_reply", "leave_topic"};
constexpr const char *kKnownToolPolicies[] = {"inherit", "allow_list", "none"};

bool contains(const char *const *values, std::size_t count, const std::string &value)
{
    for (std::size_t index = 0; index < count; ++index)
    {
        if (value == values[index])
            return true;
    }
    return false;
}

bool readInteger(const nlohmann::json &value, const char *key, int &out, std::string &error)
{
    if (!value.contains(key))
        return true;
    if (!value.at(key).is_number_integer())
    {
        error = std::string(key) + " 必须是整数";
        return false;
    }
    const std::int64_t raw = value.at(key).get<std::int64_t>();
    if (raw < 0 || raw > 100000)
    {
        error = std::string(key) + " 超出范围";
        return false;
    }
    out = static_cast<int>(raw);
    return true;
}

bool readString(const nlohmann::json &value, const char *key, std::string &out, std::string &error)
{
    if (!value.contains(key))
        return true;
    if (!value.at(key).is_string())
    {
        error = std::string(key) + " 必须是字符串";
        return false;
    }
    out = value.at(key).get<std::string>();
    return true;
}
} // namespace

bool isKnownBehaviorAction(const std::string &action)
{
    return contains(kKnownActions, sizeof(kKnownActions) / sizeof(kKnownActions[0]), action);
}

bool isKnownToolPolicy(const std::string &toolPolicy)
{
    return contains(kKnownToolPolicies,
                    sizeof(kKnownToolPolicies) / sizeof(kKnownToolPolicies[0]), toolPolicy);
}

bool BehaviorPlan::validate(std::string &error) const
{
    if (schemaVersion != kSchemaVersion)
    {
        error = "不支持的 behavior plan schema_version: " + std::to_string(schemaVersion);
        return false;
    }
    if (!isKnownBehaviorAction(action))
    {
        error = "未知 behavior plan action: " + action;
        return false;
    }
    if (speechAct.empty())
    {
        error = "speech_act 不能为空";
        return false;
    }
    if (tone.empty())
    {
        error = "tone 不能为空";
        return false;
    }
    if (maxCodepoints < 0 || maxCodepoints > 10000)
    {
        error = "max_codepoints 超出范围";
        return false;
    }
    if (!isKnownToolPolicy(toolPolicy))
    {
        error = "未知 tool_policy: " + toolPolicy;
        return false;
    }
    if (toolPolicy != "allow_list" && !allowedTools.empty())
    {
        error = "只有 allow_list 才能携带 allowed_tools";
        return false;
    }
    if (action == "no_reply" && toolPolicy != "none")
    {
        error = "no_reply 的 tool_policy 必须是 none";
        return false;
    }

    std::set<std::string> uniqueTools;
    for (const std::string &tool : allowedTools)
    {
        if (tool.empty())
        {
            error = "allowed_tools 不能包含空名称";
            return false;
        }
        if (!uniqueTools.insert(tool).second)
        {
            error = "allowed_tools 包含重复项: " + tool;
            return false;
        }
    }
    return true;
}

nlohmann::json BehaviorPlan::toJson() const
{
    return nlohmann::json{
        {"schema_version", schemaVersion},
        {"action", action},
        {"speech_act", speechAct},
        {"tone", tone},
        {"max_codepoints", maxCodepoints},
        {"tool_policy", toolPolicy},
        {"allowed_tools", allowedTools}};
}

std::optional<BehaviorPlan> BehaviorPlan::fromJson(const nlohmann::json &value, std::string &error)
{
    if (!value.is_object())
    {
        error = "behavior plan 必须是 JSON 对象";
        return std::nullopt;
    }

    BehaviorPlan plan;
    if (!value.contains("schema_version") || !value.at("schema_version").is_number_integer())
    {
        error = "缺少整数 schema_version";
        return std::nullopt;
    }
    const std::int64_t schemaVersionValue = value.at("schema_version").get<std::int64_t>();
    if (schemaVersionValue < 0 || schemaVersionValue > 100000)
    {
        error = "schema_version 超出范围";
        return std::nullopt;
    }
    plan.schemaVersion = static_cast<int>(schemaVersionValue);

    if (!value.contains("action") || !value.at("action").is_string())
    {
        error = "缺少字符串 action";
        return std::nullopt;
    }
    plan.action = value.at("action").get<std::string>();

    if (!readString(value, "speech_act", plan.speechAct, error) ||
        !readString(value, "tone", plan.tone, error) ||
        !readInteger(value, "max_codepoints", plan.maxCodepoints, error))
        return std::nullopt;

    if (!readString(value, "tool_policy", plan.toolPolicy, error))
        return std::nullopt;

    if (value.contains("allowed_tools"))
    {
        if (!value.at("allowed_tools").is_array())
        {
            error = "allowed_tools 必须是字符串数组";
            return std::nullopt;
        }
        for (const auto &tool : value.at("allowed_tools"))
        {
            if (!tool.is_string())
            {
                error = "allowed_tools 必须是字符串数组";
                return std::nullopt;
            }
            plan.allowedTools.push_back(tool.get<std::string>());
        }
    }

    if (!plan.validate(error))
        return std::nullopt;
    return plan;
}

} // namespace affect
