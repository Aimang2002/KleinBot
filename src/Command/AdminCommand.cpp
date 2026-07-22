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
    std::string cmd = utils::trim(ctx.data.plain_text);
    nlohmann::json arguments;
    if (cmd == "#开启无障碍聊天")
        arguments["action"] = "enable_accessibility_chat";
    else if (cmd == "#关闭无障碍聊天")
        arguments["action"] = "disable_accessibility_chat";
    else if (cmd == "#刷新配置文件")
        arguments["action"] = "refresh_config";
    else if (cmd == "#激活语音")
        arguments["action"] = "activate_global_voice";
    else if (cmd == "#冻结语音")
        arguments["action"] = "freeze_global_voice";
    else if (cmd == "#获取服务器inet4")
        arguments["action"] = "get_inet4";
    else if (cmd == "#获取服务器inet6")
        arguments["action"] = "get_inet6";
    else if (cmd == "#获取服务器公网IP")
        arguments["action"] = "get_public_ip";
    const auto result = action.execute(arguments, {ctx.user_id, 0});
    return {TextMessage{result.content.empty() ? "操作失败，请查看日志定位问题。" : result.content}};
}
