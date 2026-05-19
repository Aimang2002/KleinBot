#ifndef SWITCHMODELCOMMAND_H
#define SWITCHMODELCOMMAND_H

/*
 *  切换模型命令
 */
#include "Command.h"
#include "../UserSession/UserSessionService.h"
#include <unordered_set>
#include <vector>

using chatM = std::vector<std::pair<std::unordered_set<std::string>, std::vector<std::string>>>;

class SwitchModelCommand : public Command
{
public:
    SwitchModelCommand(UserSessionService &USS, const chatM &cm) : userSession(USS), chatModels(cm) {}
    bool canHandle(const std::string &message) override { return utils::check_command_exists(message, m_cmd); }
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "切换当前Agent所使用的模型，可使用“#模型列表”来获取更多模型"; }

private:
    std::string m_cmd = "#切换模型";
    UserSessionService &userSession;
    const chatM &chatModels;
};

#endif // SWITCHMODELCOMMAND_H