#include "BehaviorPlan.h"

#include <cstdint>
#include <set>

namespace affect
{
namespace
{
constexpr const char *kKnownActions[] = {"reply", "group_say", "no_reply", "leave_topic"};
constexpr const char *kKnownToolPolicies[] = {"inherit", "allow_list", "none"};
constexpr const char *kKnownMoves[] = {"answer", "follow", "agree", "comfort", "tease", "ask", "ack"};
constexpr const char *kKnownLengths[] = {"short", "one", "few"};

bool contains(const char *const *values, std::size_t count, const std::string &value)
{
    for (std::size_t index = 0; index < count; ++index)
    {
        if (value == values[index])
            return true;
    }
    return false;
}

std::size_t codepoints(const std::string &text)
{
    std::size_t count = 0;
    for (const unsigned char byte : text)
        if ((byte & 0xC0) != 0x80)
            ++count;
    return count;
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

bool readTools(const nlohmann::json &value, std::vector<std::string> &out, std::string &error)
{
    if (!value.contains("allowed_tools"))
        return true;
    if (!value.at("allowed_tools").is_array())
    {
        error = "allowed_tools 必须是字符串数组";
        return false;
    }
    for (const auto &tool : value.at("allowed_tools"))
    {
        if (!tool.is_string())
        {
            error = "allowed_tools 必须是字符串数组";
            return false;
        }
        out.push_back(tool.get<std::string>());
    }
    return true;
}

// 派生文字字段：不跨行、不超过上限
bool validNote(const std::string &text, const char *key, std::string &error)
{
    if (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos)
    {
        error = std::string(key) + " 不能换行";
        return false;
    }
    if (codepoints(text) > BehaviorPlan::kMaxNoteCodepoints)
    {
        error = std::string(key) + " 超过 " + std::to_string(BehaviorPlan::kMaxNoteCodepoints) +
                " 个码点";
        return false;
    }
    return true;
}

bool validateTools(const BehaviorPlan &plan, std::string &error)
{
    if (!isKnownToolPolicy(plan.toolPolicy))
    {
        error = "未知 tool_policy: " + plan.toolPolicy;
        return false;
    }
    if (plan.toolPolicy != "allow_list" && !plan.allowedTools.empty())
    {
        error = "只有 allow_list 才能携带 allowed_tools";
        return false;
    }
    if (plan.action == "no_reply" && plan.toolPolicy != "none")
    {
        error = "no_reply 的 tool_policy 必须是 none";
        return false;
    }

    std::set<std::string> uniqueTools;
    for (const std::string &tool : plan.allowedTools)
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

bool validateV1(const BehaviorPlan &plan, std::string &error)
{
    if (plan.speechAct.empty())
    {
        error = "speech_act 不能为空";
        return false;
    }
    if (plan.tone.empty())
    {
        error = "tone 不能为空";
        return false;
    }
    if (plan.maxCodepoints < 0 || plan.maxCodepoints > 10000)
    {
        error = "max_codepoints 超出范围";
        return false;
    }
    return true;
}

bool validateV2(const BehaviorPlan &plan, std::string &error)
{
    const bool speaking = isSpeakingAction(plan.action);
    if (speaking ? !isKnownMove(plan.move) : !(plan.move.empty() || isKnownMove(plan.move)))
    {
        error = "未知 move: " + plan.move;
        return false;
    }
    if (speaking ? !isKnownLength(plan.length)
                 : !(plan.length.empty() || isKnownLength(plan.length)))
    {
        error = "未知 length: " + plan.length;
        return false;
    }
    if (plan.replyTo && (plan.replyTo->empty() ||
                         plan.replyTo->size() > BehaviorPlan::kMaxReplyToBytes))
    {
        error = "reply_to 必须是非空的 message_id";
        return false;
    }
    return validNote(plan.occasion, "occasion", error) &&
           validNote(plan.reactionToLast, "reaction_to_last", error) &&
           validNote(plan.why, "why", error);
}

std::optional<BehaviorPlan> parseV2(const nlohmann::json &value, BehaviorPlan plan,
                                    std::string &error)
{
    if (value.contains("reply_to") && !value.at("reply_to").is_null())
    {
        if (!value.at("reply_to").is_string())
        {
            error = "reply_to 必须是字符串或 null";
            return std::nullopt;
        }
        plan.replyTo = value.at("reply_to").get<std::string>();
    }
    if (!readString(value, "move", plan.move, error) ||
        !readString(value, "length", plan.length, error) ||
        !readString(value, "occasion", plan.occasion, error) ||
        !readString(value, "reaction_to_last", plan.reactionToLast, error) ||
        !readString(value, "why", plan.why, error) ||
        !readString(value, "tool_policy", plan.toolPolicy, error) ||
        !readTools(value, plan.allowedTools, error))
        return std::nullopt;
    if (!plan.validate(error))
        return std::nullopt;
    return plan;
}

std::optional<BehaviorPlan> parseV1(const nlohmann::json &value, BehaviorPlan plan,
                                    std::string &error)
{
    if (!readString(value, "speech_act", plan.speechAct, error) ||
        !readString(value, "tone", plan.tone, error) ||
        !readInteger(value, "max_codepoints", plan.maxCodepoints, error) ||
        !readString(value, "tool_policy", plan.toolPolicy, error) ||
        !readTools(value, plan.allowedTools, error))
        return std::nullopt;
    if (!plan.validate(error))
        return std::nullopt;
    return plan;
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

bool isKnownMove(const std::string &move)
{
    return contains(kKnownMoves, sizeof(kKnownMoves) / sizeof(kKnownMoves[0]), move);
}

bool isKnownLength(const std::string &length)
{
    return contains(kKnownLengths, sizeof(kKnownLengths) / sizeof(kKnownLengths[0]), length);
}

bool isSpeakingAction(const std::string &action)
{
    return action == "reply" || action == "group_say";
}

bool BehaviorPlan::validate(std::string &error) const
{
    if (schemaVersion != kSchemaVersion && schemaVersion != kSchemaVersionV2)
    {
        error = "不支持的 behavior plan schema_version: " + std::to_string(schemaVersion);
        return false;
    }
    if (!isKnownBehaviorAction(action))
    {
        error = "未知 behavior plan action: " + action;
        return false;
    }
    if (!(isV2() ? validateV2(*this, error) : validateV1(*this, error)))
        return false;
    return validateTools(*this, error);
}

nlohmann::json BehaviorPlan::toJson() const
{
    if (!isV2())
        return nlohmann::json{
            {"schema_version", schemaVersion},
            {"action", action},
            {"speech_act", speechAct},
            {"tone", tone},
            {"max_codepoints", maxCodepoints},
            {"tool_policy", toolPolicy},
            {"allowed_tools", allowedTools}};

    nlohmann::json json{{"schema_version", schemaVersion}, {"action", action}};
    json["reply_to"] = replyTo ? nlohmann::json(*replyTo) : nlohmann::json(nullptr);
    if (!move.empty())
        json["move"] = move;
    if (!length.empty())
        json["length"] = length;
    json["occasion"] = occasion;
    if (!reactionToLast.empty())
        json["reaction_to_last"] = reactionToLast;
    json["why"] = why;
    json["tool_policy"] = toolPolicy;
    json["allowed_tools"] = allowedTools;
    return json;
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

    if (plan.schemaVersion == kSchemaVersionV2)
        return parseV2(value, std::move(plan), error);
    if (plan.schemaVersion == kSchemaVersion)
        return parseV1(value, std::move(plan), error);
    error = "不支持的 behavior plan schema_version: " + std::to_string(plan.schemaVersion);
    return std::nullopt;
}

} // namespace affect
