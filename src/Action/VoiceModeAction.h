#ifndef VOICE_MODE_ACTION_H
#define VOICE_MODE_ACTION_H

#include "Action.h"
#include "../Port/ToolNames.h"
#include "../UserSession/UserSessionService.h"

class VoiceModeAction : public Action
{
public:
    VoiceModeAction(UserSessionService &userSession, bool &globalVoice)
        : userSession(userSession), globalVoice(globalVoice) {}

    const ActionDescriptor &descriptor() const override
    {
        static const ActionDescriptor value{
            KleinSetVoiceModeToolName,
            "开启或关闭当前用户的语音回复。用户说开启语音、打开语音、关闭语音时调用。",
            {
                {"type", "object"},
                {"properties", {{"enabled", {{"type", "boolean"}}}}},
                {"required", {"enabled"}}
            },
            true, false};
        return value;
    }

    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &) const override
    {
        return guidanceText(scene);
    }

    // 规则文本（静态：不依赖 UserSessionService，金样测试直接取用）
    static std::string guidanceText(GuidanceScene scene)
    {
        return scene == GuidanceScene::Conversation ? "当用户要求开启或关闭语音时调用 set_voice_mode。" : "";
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
