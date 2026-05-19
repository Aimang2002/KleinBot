#ifndef QUERYMODELCOMMAND_H
#define QUERYMODELCOMMAND_H

/*
 *  查询当前模型命令
 */
#include "Command.h"

class QueryModelCommand : public Command
{
public:
    QueryModelCommand(std::function<std::string(uint64_t)> getter) : getCurrentModelName(std::move(getter)) {}
    bool canHandle(const std::string &message) override { return message == "#查询当前模型"; }
    CommandResult execute(const CommandContext &ctx);
    std::string help() const override { return "可查询当前正在使用的模型"; }

private:
    std::function<std::string(uint64_t)> getCurrentModelName;
};

#endif // QUERYMODELCOMMAND_H