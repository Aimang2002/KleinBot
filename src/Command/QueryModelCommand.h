#ifndef QUERYMODELCOMMAND_H
#define QUERYMODELCOMMAND_H

/*
 *  查询当前模型命令
 */
#include "Command.h"
#include "../Action/Action.h"

class QueryModelCommand : public Command
{
public:
    explicit QueryModelCommand(Action &action) : action(action) {}
    bool canHandle(const std::string &message) override { return message == "#查询当前模型"; }
    CommandResult execute(const CommandContext &ctx);
    std::string help() const override { return "可查询当前正在使用的模型"; }

private:
    Action &action;
};

#endif // QUERYMODELCOMMAND_H
