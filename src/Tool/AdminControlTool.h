#ifndef ADMIN_CONTROL_TOOL_H
#define ADMIN_CONTROL_TOOL_H

#include "Tool.h"
#include "../ComputerStatus/ComputerStatus.h"
#include "../ConfigManager/ConfigManager.h"
#include "../../Library/nlohmann/json.hpp"
#include <functional>

class AdminControlTool : public Tool
{
public:
    AdminControlTool(ComputerStatus &computerStatus, bool &accessibilityChat,
                     bool &globalVoice, std::function<void()> refresh)
        : computerStatus(computerStatus), accessibilityChat(accessibilityChat),
          globalVoice(globalVoice), refresh(std::move(refresh)) {}

    std::string name() const override { return "admin_control"; }

    std::string description() const override
    {
        return "执行管理员控制操作。仅管理员可以调用。支持开启或关闭无障碍聊天、刷新配置、激活或冻结全局语音、查询服务器网络信息。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"action":{"type":"string","enum":["enable_accessibility_chat","disable_accessibility_chat","refresh_config","activate_global_voice","freeze_global_voice","get_inet4","get_inet6","get_public_ip"]}},"required":["action"]})";
    }

    bool requiresAdmin() const override { return true; }

    ToolResult execute(const std::string &args, const ToolContext &) override
    {
        try
        {
            const std::string action = nlohmann::json::parse(args).value("action", "");
            if (action == "enable_accessibility_chat")
            {
                accessibilityChat = true;
                return {"无障碍聊天已开启！", {}, {}, true};
            }
            if (action == "disable_accessibility_chat")
            {
                accessibilityChat = false;
                return {"无障碍聊天已关闭！", {}, {}, true};
            }
            if (action == "refresh_config")
            {
                refresh();
                return {"配置文件已刷新。", {}, {}, true};
            }
            if (action == "activate_global_voice")
            {
                globalVoice = true;
                return {"全局语音已激活。", {}, {}, true};
            }
            if (action == "freeze_global_voice")
            {
                globalVoice = false;
                return {"全局语音已冻结。", {}, {}, true};
            }
            if (action == "get_inet4")
                return {computerStatus.getInet4(), {}, {}, true};
            if (action == "get_inet6")
                return {computerStatus.getInet6(), {}, {}, true};
            if (action == "get_public_ip")
                return {computerStatus.getPublicIP(), {}, {}, true};
            return {"错误：未知的管理员操作。", {}, {}, true};
        }
        catch (const std::exception &error)
        {
            return {"错误：管理员操作参数无效：" + std::string(error.what()), {}, {}, true};
        }
    }

private:
    ComputerStatus &computerStatus;
    bool &accessibilityChat;
    bool &globalVoice;
    std::function<void()> refresh;
};

#endif // ADMIN_CONTROL_TOOL_H
