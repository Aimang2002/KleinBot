#include "ModelListCommand.h"
#include <fstream>

CommandResult ModelListCommand::execute(const CommandContext &ctx)
{

    if (this->chatModels.empty())
    {
        return {"未装载其他模型"};
    }

    std::string content = "当前装载的模型如下：\n";
    for (const auto &pair : chatModels)
    {
        for (const auto &name : pair.first)
        {
            content.append("\n" + name + "\n");
        }
    }
    return {content};
}
