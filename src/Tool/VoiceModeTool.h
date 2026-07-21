#ifndef VOICE_MODE_TOOL_H
#define VOICE_MODE_TOOL_H

#include "Tool.h"
#include "../UserSession/UserSessionService.h"
#include "../../Library/nlohmann/json.hpp"

class VoiceModeTool : public Tool
{
public:
    VoiceModeTool(UserSessionService &userSession, bool &globalVoice)
        : userSession(userSession), globalVoice(globalVoice) {}

    std::string name() const override { return "set_voice_mode"; }

    std::string description() const override
    {
        return "开启或关闭当前用户的语音回复。用户说开启语音、打开语音、关闭语音时调用。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"enabled":{"type":"boolean","description":"是否开启语音回复"}},"required":["enabled"]})";
    }

    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        try
        {
            const auto arguments = nlohmann::json::parse(args);
            const bool enabled = arguments.value("enabled", false);
            if (!globalVoice)
                return {"管理员尚未激活语音功能。", {}, {}, true};
            userSession.voiceSwitch(ctx.user_id, enabled);
            return {enabled ? "语音已开启。" : "语音已关闭。", {}, {}, true};
        }
        catch (const std::exception &error)
        {
            return {"错误：语音设置失败：" + std::string(error.what()), {}, {}, true};
        }
    }

private:
    UserSessionService &userSession;
    bool &globalVoice;
};

#endif // VOICE_MODE_TOOL_H
