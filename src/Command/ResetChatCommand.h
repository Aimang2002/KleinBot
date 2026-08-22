#ifndef RESETCHATCOMMAND_H
#define RESETCHATCOMMAND_H

/*
 *  重置对话命令（轻量）：清空当前上下文窗口，历史记录保留且仍可召回
 */
#include "Command.h"
#include "../UserSession/UserSessionService.h"
class ResetChatCommand : public Command
{
public:
    ResetChatCommand(UserSessionService &USS) : userSession(USS) {}
    bool canHandle(const std::string &message) override { return message == "#重置对话"; }
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "清空当前对话上下文，历史记录保留且仍可召回"; };

private:
    UserSessionService &userSession;
};
#endif // RESETCHATCOMMAND_h
