#ifndef RESETCONTEXTCOMMAND_H
#define RESETCONTEXTCOMMAND_H

/*
 *  重置上下文命令（彻底）：删除内存、SQLite 对话、长期记忆和图片资源
 */
#include "Command.h"
#include "../UserSession/UserSessionService.h"
class ResetContextCommand : public Command
{
public:
    ResetContextCommand(UserSessionService &USS) : userSession(USS) {}
    bool canHandle(const std::string &message) override { return message == "#重置上下文"; }
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "彻底删除全部对话历史、长期记忆与图片资源"; };

private:
    UserSessionService &userSession;
};
#endif // RESETCONTEXTCOMMAND_H
