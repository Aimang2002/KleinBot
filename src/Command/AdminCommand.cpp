#include "AdminCommand.h"
#include "../utils/Utils.hpp"
#include "../JsonParse/JsonParse.h"

bool AdminCommand::canHandle(const std::string &message)
{
    std::string target = utils::trim(message);
    return this->cmds.find(target) != this->cmds.end();
}

CommandResult AdminCommand::execute(const CommandContext &ctx)
{
    std::string str;
    std::string cmd = utils::trim(ctx.data.raw_message);
    if (cmd == "#开启无障碍聊天")
    {
        this->m_accessibility_chat = true;
        str = "无障碍聊天已开启！";
    }
    else if (cmd == "#关闭无障碍聊天")
    {
        this->m_accessibility_chat = false;
        str = "无障碍聊天已关闭！";
    }
    else if (cmd == "#刷新配置文件")
    {
        m_refresh();
        str = "配置文件已刷新";
    }
    else if (cmd == "#激活语音")
    {
        this->m_global_Voice = true;
        str = "已激活！";
    }
    else if (cmd == "#冻结语音")
    {
        this->m_global_Voice = false;
        str = "已冻结！";
    }
    else if (cmd == "#获取服务器inet4")
    {
        str = this->m_PCStatus.getInet4();
    }
    else if (cmd == "#获取服务器inet6")
    {
        str = this->m_PCStatus.getInet6();
    }
    else if (cmd == "#获取服务器公网IP")
    {
        str = this->m_PCStatus.getPublicIP();
    }
    std::string response = std::string(!str.empty() ? str : "操作失败，请查看日志定位问题。");
    response = JsonParse::getInstance().toJson(response);
    return {response};
}
