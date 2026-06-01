#include "SwitchModelCommand.h"

CommandResult SwitchModelCommand::execute(const CommandContext &ctx)
{
    std::string modelName = utils::CP_split(ctx.data.raw_message, m_cmd);
    if (modelName.empty())
    {
        return {"请提供需要切换的模型！"};
    }
    modelName = utils::trim(modelName);
    const ChatModel *m = this->chatModels.find(modelName);
    if (!m)
    {
        return {"未找到模型名称！"};
    }

    std::pair<std::string, std::vector<std::string>> newModel;
    newModel.first = modelName;
    newModel.second.push_back(m->api_key);
    newModel.second.push_back(m->endpoint);
    newModel.second.push_back(m->api_standard);
    this->userSession.switchModel(ctx.user_id, newModel);
    return {"模型切换成功。"};
}
