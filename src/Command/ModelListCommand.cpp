#include "ModelListCommand.h"

CommandResult ModelListCommand::execute(const CommandContext &ctx)
{
    if (this->chatModels.all().empty())
    {
        return {"未装载其他模型"};
    }

    std::string content = "当前装载的模型如下：\n";
    for (const auto &model : this->chatModels.all())
    {
        for (const auto &name : model.modelList)
        {
            content.append("\n" + name + "\n");
        }
    }
    return {content};
}
