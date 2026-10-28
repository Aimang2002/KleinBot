#ifndef COMMAND_REGISTRY_H
#define COMMAND_REGISTRY_H

#include "Command.h"
#include <vector>
#include <memory>
#include <optional>

class CommandRegistry
{
public:
    explicit CommandRegistry(uint64_t managerId) : managerId(managerId) {}
    void registryCommand(std::unique_ptr<Command> cmd);
    // 查找并执行匹配命令，并且返回是否找到匹配
    std::optional<CommandResult> execute(const std::string &message, const CommandContext &ctx);
    // 只判断是否命中某条命令，不执行（组合根用它把命令消息排除在群内容/话题会话之外）
    bool matches(const std::string &message) const;
    // 获取所有帮助命令
    // std::string getAllHelp() const {};

private:
    uint64_t managerId;
    std::vector<std::unique_ptr<Command>> commands;
};

#endif