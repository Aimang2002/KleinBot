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
    if (this->global_voice == false)
    {
        return {"管理员限制的了语音的开启和关闭！"};
    }

    bool newStatus = false;
    std::string cmd = utils::trim(ctx.data.plain_text);
    newStatus = cmd == "#开启语音";
    this->userSession.voiceSwitch(ctx.user_id, newStatus);

    std::string response_message = (newStatus ? "语音已开启。" : "语音已关闭。");
    return {response_message};
}
