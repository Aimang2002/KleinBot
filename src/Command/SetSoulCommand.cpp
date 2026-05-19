#include "SetSoulCommand.h"
#include "../JsonParse/JsonParse.h"

CommandResult SetSoulCommand::execute(const CommandContext &ctx)
{
    std::string prompt = utils::CP_split(ctx.data.raw_message, this->m_cmd);
    if (prompt.empty())
    {
        return {"未找到人格描述！", MessageType::Text, false};
    }
    prompt = utils::trim(prompt);
    prompt = JsonParse::getInstance().toJson(prompt);
    this->userSession.setPersonality(ctx.user_id, prompt);
    return {"人格更新成功。"};
}
