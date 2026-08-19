#ifndef VOICE_MODE_ACTION_H
#define VOICE_MODE_ACTION_H

#include "Action.h"
#include "../UserSession/UserSessionService.h"

class VoiceModeAction : public Action
{
public:
    VoiceModeAction(UserSessionService &userSession, bool &globalVoice)
        : userSession(userSession), globalVoice(globalVoice) {}

    const ActionDescriptor &descriptor() const override
    {
        static const ActionDescriptor value{
            "set_voice_mode",
            "开启或关闭当前用户的语音回复。用户说开启语音、打开语音、关闭语音时调用。",
            {
                {"type", "object"},
                {"properties", {{"enabled", {{"type", "boolean"}}}}},
                {"required", {"enabled"}}
            },
            true, false};
        return value;
    }

    ActionResult execute(const nlohmann::json &arguments, const ActionContext &context) override
    {
        const bool enabled = arguments.value("enabled", false);
        if (!globalVoice)
            return {"管理员尚未激活语音功能。", {}, {}, true};
        userSession.voiceSwitch(context.user_id, enabled);
        return {enabled ? "语音已开启。" : "语音已关闭。", {}, {}, true};
    }

private:
    UserSessionService &userSession;
    bool &globalVoice;
};

#endif // VOICE_MODE_ACTION_H
