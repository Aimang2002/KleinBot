#ifndef RESETCHATCOMMAND_H
#define RESETCHATCOMMAND_H

/*
 *  重置对话命令
 */
#include "Command.h"
#include "../UserSession/UserSessionService.h"
class ResetChatCommand : public Command
{
public:
    ResetChatCommand(UserSessionService &USS) : userSession(USS) {}
    bool canHandle(const std::string &message) override { return message == "#重置对话"; }
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "删除所有的上下文"; };

private:
    UserSessionService &userSession;
};
#endif // RESETCHATCOMMAND_h