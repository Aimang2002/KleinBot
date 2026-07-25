#ifndef ADMIN_CONTROL_ACTION_H
#define ADMIN_CONTROL_ACTION_H

#include "Action.h"
#include "../ComputerStatus/ComputerStatus.h"
#include <functional>

class AdminControlAction : public Action
{
public:
    AdminControlAction(ComputerStatus &computerStatus, bool &accessibilityChat,
                       bool &globalVoice, std::function<void()> refresh)
        : computerStatus(computerStatus), accessibilityChat(accessibilityChat),
          globalVoice(globalVoice), refresh(std::move(refresh)) {}

    const ActionDescriptor &descriptor() const override
    {
        static const ActionDescriptor value{
            "admin_control",
            "执行管理员控制操作。仅管理员可以调用。支持无障碍聊天、配置刷新、全局语音和服务器网络查询。",
            {{"type", "object"},
             {"properties", {{"action", {{"type", "string"}, {"enum", {"enable_accessibility_chat", "disable_accessibility_chat", "refresh_config", "activate_global_voice", "freeze_global_voice", "get_inet4", "get_inet6", "get_public_ip"}}}}}},
             {"required", {"action"}}},
            true,
            true};
        return value;
    }

    ActionResult execute(const nlohmann::json &arguments, const ActionContext &) override
    {
        const std::string action = arguments.value("action", "");
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
            return {"模型注册表已刷新；应用配置未热更新。", {}, {}, true};
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

private:
    ComputerStatus &computerStatus;
    bool &accessibilityChat;
    bool &globalVoice;
    std::function<void()> refresh;
};

#endif // ADMIN_CONTROL_ACTION_H
