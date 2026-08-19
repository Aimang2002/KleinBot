#ifndef VOICESWITCHCOMMAND_H
#define VOICESWITCHCOMMAND_H
/*
 * 语音切换命令
 */
#include "Command.h"
#include "../utils/Utils.hpp"
#include "../UserSession/UserSessionService.h"
#include "../Action/Action.h"

class VoiceSwitchCommand : public Command
{
public:
    explicit VoiceSwitchCommand(Action &action) : action(action) {}
    bool canHandle(const std::string &message) override;
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "根据命令，关闭或者开启Agent的语音回答。"; }

private:
    Action &action;
};

#endif // VOICESWITCHCOMMAND_H
