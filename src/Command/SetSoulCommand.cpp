#include "SetSoulCommand.h"
#include "../JsonParse/JsonParse.h"

CommandResult SetSoulCommand::execute(const CommandContext &ctx)
{
    if (ctx.data.raw_message == "#人格还原")
    {
        this->userSession.resetPersonality(ctx.user_id);
        return {"人格还原成功。"};
    }
    else
    {
        std::string prompt = utils::CP_split(ctx.data.raw_message, this->m_cmd);
        if (prompt.empty())
        {
            return {"未找到人格描述！"};
        }
        prompt = utils::trim(prompt);
        this->userSession.setPersonality(ctx.user_id, prompt);
        return {"人格更新成功。"};
    }
}
