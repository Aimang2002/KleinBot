#include "CommandRegistry.h"

// 生命周期转移
void CommandRegistry::registryCommand(std::unique_ptr<Command> cmd)
{
    commands.push_back(std::move(cmd));
}

std::optional<CommandResult> CommandRegistry::execute(const std::string &message, const CommandContext &ctx)
{
    for (const auto &c : commands)
    {
        // 权限检查
        if (c->canHandle(message))
        {
            if (c->requiresAdmin())
            {
                if (ctx.user_id != managerId)
                    return CommandResult{TextMessage{"权限不足！"}};
            }
            return c->execute(ctx);
        }
    }
    return std::nullopt;
}