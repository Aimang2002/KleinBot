#include "SetSoulCommand.h"
#include "../JsonParse/JsonParse.h"

CommandResult SetSoulCommand::execute(const CommandContext &ctx)
{
    if (ctx.data.plain_text == "#人格还原")
    {
        this->userSession.resetPersonality(ctx.user_id);
        return {TextMessage{"人格还原成功。"}};
    }
    else
    {
        std::string prompt = utils::CP_split(ctx.data.plain_text, this->m_cmd);
        if (prompt.empty())
        {
            return {TextMessage{"未找到人格描述！"}};
        }
        prompt = utils::trim(prompt);
        this->userSession.setPersonality(ctx.user_id, prompt);
        return {TextMessage{"人格更新成功。"}};
    }
}
