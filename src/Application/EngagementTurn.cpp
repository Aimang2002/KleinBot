#include "EngagementTurn.h"
#include "../Port/ToolNames.h"
#include "../Tool/ToolArgumentParser.h"
#include "../utils/TextTokenize.h"
#include "../utils/Utils.hpp"

namespace
{
std::string jsonArg(const nlohmann::json &arguments, const char *key)
{
    if (arguments.is_object() && arguments.contains(key) && arguments[key].is_string())
        return arguments[key].get<std::string>();
    return {};
}
} // namespace

EngagementTurn parseEngagementTurn(const ChatResponse &response)
{
    EngagementTurn turn;
    for (const auto &call : response.tool_calls)
    {
        if (call.name != "group_say" && call.name != KleinSendVoiceToolName &&
            call.name != KleinLeaveTopicToolName)
            continue;
        const nlohmann::json arguments = parseToolArguments(call.arguments);
        if (call.name == "group_say" || call.name == KleinSendVoiceToolName)
        {
            turn.kind = EngagementTurn::Kind::Speak;
            turn.text = jsonArg(arguments, "text");
            turn.voice = call.name == KleinSendVoiceToolName;
            turn.reason.clear();
            return turn;
        }
        turn.kind = EngagementTurn::Kind::Leave;
        turn.reason = jsonArg(arguments, "reason");
        turn.text = jsonArg(arguments, "final_text");
        turn.stepBack = arguments.is_object() && arguments.contains(KleinLeaveTopicStepBackParam) &&
                        arguments[KleinLeaveTopicStepBackParam].is_boolean() &&
                        arguments[KleinLeaveTopicStepBackParam].get<bool>();
        return turn;
    }
    if (!response.tool_calls.empty())
        return turn; // 只有未知工具：视为本拍沉默
    // 没有任何工具调用：一律沉默，content 不发出（L3 护栏；2026-11-14 之前非空 content
    // 会被当成发言发进群）。只把长度带给调用方记日志，原文不带出
    turn.unspokenChars = utils::utf8Characters(utils::trim(response.content)).size();
    return turn;
}
