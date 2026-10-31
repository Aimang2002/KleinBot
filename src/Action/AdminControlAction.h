#ifndef ADMIN_CONTROL_ACTION_H
#define ADMIN_CONTROL_ACTION_H

#include "Action.h"
#include "../Port/ToolNames.h"
#include "../ComputerStatus/ComputerStatus.h"
#include <functional>

class AdminControlAction : public Action
{
public:
    AdminControlAction(ComputerStatus &computerStatus,
                       bool &globalVoice, std::function<std::string()> refresh)
        : computerStatus(computerStatus),
          globalVoice(globalVoice), refresh(std::move(refresh)) {}

    const ActionDescriptor &descriptor() const override
    {
        static const ActionDescriptor value{
            KleinAdminControlToolName,
            "执行管理员控制操作。仅管理员可以调用。支持配置刷新、全局语音和服务器网络查询。",
            {{"type", "object"},
             {"properties", {{"action", {{"type", "string"}, {"enum", {"refresh_config", "activate_global_voice", "freeze_global_voice", "get_inet4", "get_inet6", "get_public_ip"}}}}}},
             {"required", {"action"}}},
            true,
            true};
        return value;
    }

    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &) const override
    {
        return scene == GuidanceScene::Conversation
                   ? "管理员控制操作只能调用 admin_control，系统会在工具执行前校验管理员身份。"
                   : "";
    }

    ActionResult execute(const nlohmann::json &arguments, const ActionContext &) override
    {
        const std::string action = arguments.value("action", "");
        if (action == "refresh_config")
            return {refresh(), {}, {}, true};
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
    bool &globalVoice;
    std::function<std::string()> refresh;
};

#endif // ADMIN_CONTROL_ACTION_H
