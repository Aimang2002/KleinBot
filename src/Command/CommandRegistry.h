#ifndef COMMAND_REGISTRY_H
#define COMMAND_REGISTRY_H

#include "Command.h"
#include <vector>
#include <memory>

class CommandRegistry
{
public:
    void registryCommand(std::unique_ptr<Command> cmd);
    // 查找并执行匹配命令，并且返回是否找到匹配
    CommandResult execute(const std::string &message, const CommandContext &ctx);
    // 获取所有帮助命令
    std::string getAllHelp() const;

private:
    std::vector<std::unique_ptr<Command>> commands;
};

#endif