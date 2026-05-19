#ifndef SETSOULCOMMAND_H
#define SETSOULCOMMAND_H

/*
 *  设置人格命令
 */
#include "Command.h"
#include "../utils/Utils.hpp"
#include "../UserSession/UserSessionService.h"

class SetSoulCommand : public Command
{
public:
    SetSoulCommand(UserSessionService &USS) : userSession(USS) {}
    bool canHandle(const std::string &message) override { return utils::check_command_exists(message, m_cmd); };
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "设置系统级的对话约束，实现Agent的身份绑定、行为规则约束、安全与边界、全局上下文注入等功能"; }

private:
    std::string m_cmd = "#设置人格";
    UserSessionService &userSession;
};

#endif // SETSOULCOMMAND_H