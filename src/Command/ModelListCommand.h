#ifndef MODELLIST_COMMAND_H
#define MODELLIST_COMMAND_H

/*
 *  输出模型列表命令
 */
#include <iostream>
#include <unordered_set>
#include <vector>
#include "Command.h"

class ModelListCommand : public Command
{
public:
    using ModelList = std::vector<std::pair<std::unordered_set<std::string>, std::vector<std::string>>>;
    ModelListCommand(const ModelList &models) : chatModels(models) {}
    bool canHandle(const std::string &message) override { return message == "#模型列表"; }
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "查询当前机器人装载的模型数量"; }

private:
    const ModelList &chatModels;
};

#endif // MODELLIST_COMMAND_H