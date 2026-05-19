#ifndef REMOVECONTEXTCOMMAND_H
#define REMOVECONTEXTCOMMAND_H

/*
 *  删除上条对话命令
 */
#include "Command.h"
#include "../UserSession/UserSessionService.h"

class RemoveContextCommand : public Command
{
public:
    RemoveContextCommand(UserSessionService &USS) : userSession(USS) {}
    bool canHandle(const std::string &message) override { return (message == "#删除上条对话" || message == "#rewind" || message == "#undo"); }
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "删除目前对话中的最后一条对话（问题和结果）"; }

private:
    UserSessionService &userSession;
};

#endif // REMOVECONTEXTCOMMAND_H