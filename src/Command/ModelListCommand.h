#ifndef MODELLIST_COMMAND_H
#define MODELLIST_COMMAND_H

/*
 *  输出模型列表命令
 */
#include "Command.h"
#include "../ModelRegistry/ChatModel.h"
#include "../ModelRegistry/ModelRegistry.h"

class ModelListCommand : public Command
{
public:
    ModelListCommand(const ModelRegistry &mr) : chatModels(mr) {}
    bool canHandle(const std::string &message) override { return message == "#模型列表"; }
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "查询当前机器人装载的模型数量"; }

private:
    const ModelRegistry &chatModels;
};

#endif // MODELLIST_COMMAND_H