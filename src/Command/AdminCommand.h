#ifndef ADMINCOMMAND_H
#define ADMINCOMMAND_H

/*
 *  负责管理员的所有命令
 */
#include "Command.h"
#include "../ComputerStatus/ComputerStatus.h"
#include <unordered_set>

class AdminCommand : public Command
{
public:
    AdminCommand(ComputerStatus &PCStatus, bool &accessibility_chat, bool &global_Voice, std::function<void()> refresh) : m_PCStatus(PCStatus), m_accessibility_chat(accessibility_chat), m_global_Voice(global_Voice), m_refresh(refresh) {}
    bool canHandle(const std::string &message) override;
    CommandResult execute(const CommandContext &ctx) override;
    bool requiresAdmin() const override { return true; }
    std::string help() const override { return "提供管理员命令，该命令只能由管理员身份触发。"; }

private:
    bool &m_accessibility_chat;
    bool &m_global_Voice;
    ComputerStatus &m_PCStatus;
    std::function<void()> m_refresh;
    std::unordered_set<std::string> cmds = {"#开启无障碍聊天", "#关闭无障碍聊天", "#刷新配置文件", "#激活语音", "#冻结语音", "#获取服务器inet4", "#获取服务器inet6", "#获取服务器公网IP"};
};

#endif // ADMINCOMMAND_H
