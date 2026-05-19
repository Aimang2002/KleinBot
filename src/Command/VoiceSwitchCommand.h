#ifndef VOICESWITCHCOMMAND_H
#define VOICESWITCHCOMMAND_H
/*
 * 语音切换命令
 */
#include "Command.h"
#include "../UserSession/UserSessionService.h"
class VoiceSwitchCommand : public Command
{
public:
    VoiceSwitchCommand(UserSessionService &USS, bool &tag) : userSession(USS), global_voice(tag) {}
    bool canHandle(const std::string &message) override;
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "根据命令，关闭或者开启Agent的语音回答。"; }

private:
    UserSessionService &userSession;
    bool &global_voice;
};

#endif // VOICESWITCHCOMMAND_H