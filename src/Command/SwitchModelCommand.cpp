#include "SwitchModelCommand.h"

CommandResult SwitchModelCommand::execute(const CommandContext &ctx)
{
    std::string modelName = utils::CP_split(ctx.data.raw_message, m_cmd);
    if (modelName.empty())
    {
        return {"未找到模型名称！"};
    }
    modelName = utils::trim(modelName);

    for (const auto &model : this->chatModels)
    {
        if (model.first.find(modelName) != model.first.end())
        {
            std::pair<std::string, std::vector<std::string>> newModel;
            newModel.first = modelName;
            newModel.second.push_back(model.second[0]);
            newModel.second.push_back(model.second[1]);
            newModel.second.push_back(model.second[2]);
            this->userSession.switchModel(ctx.user_id, newModel);
            return {"模型切换成功。"};
        }
    }
    return {"模型切换失败"};
}
