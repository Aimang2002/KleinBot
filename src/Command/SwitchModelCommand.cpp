#include "SwitchModelCommand.h"

CommandResult SwitchModelCommand::execute(const CommandContext &ctx)
{
    std::string modelName = utils::CP_split(ctx.data.plain_text, m_cmd);
    if (modelName.empty())
    {
        return {TextMessage{"请提供需要切换的模型！"}};
    }
    modelName = utils::trim(modelName);
    const ChatModel *m = this->chatModels.find(modelName);
    if (!m)
    {
        return {TextMessage{"未找到模型名称！"}};
    }
    this->userSession.switchModel(ctx.user_id, modelName);
    return {TextMessage{"模型切换成功。"}};
}
