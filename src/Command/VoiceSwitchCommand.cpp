#include "VoiceSwitchCommand.h"

bool VoiceSwitchCommand::canHandle(const std::string &message)
{
    std::string cmd = utils::trim(message);
    if (cmd == "#开启语音")
    {
        return true;
    }
    else if (cmd == "#关闭语音")
    {
        return true;
    }
    return false;
}

CommandResult VoiceSwitchCommand::execute(const CommandContext &ctx)
{
    std::string cmd = utils::trim(ctx.data.plain_text);
    nlohmann::json arguments;
    arguments["enabled"] = cmd == "#开启语音";
    const auto result = action.execute(arguments, {ctx.user_id, 0});
    return {TextMessage{result.content}};
}
