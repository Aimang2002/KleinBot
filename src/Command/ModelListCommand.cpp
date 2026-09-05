#include "ModelListCommand.h"

CommandResult ModelListCommand::execute(const CommandContext &ctx)
{
    const std::vector<ChatModel> models = this->chatModels.all();
    if (models.empty())
    {
        return {TextMessage{"未装载其他模型"}};
    }

    std::string content = "当前装载的模型如下：\n";
    for (const auto &model : models)
    {
        for (const auto &name : model.modelList)
        {
            content.append("\n" + name + "\n");
        }
    }
    return {TextMessage{content}};
}
