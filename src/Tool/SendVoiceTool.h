#ifndef SEND_VOICE_TOOL_H
#define SEND_VOICE_TOOL_H

#include "Tool.h"
#include "ToolArgumentParser.h"
#include "../Log/Log.h"
#include "../Port/ToolNames.h"
#include "../utils/Live.h"
#include "../utils/Utils.hpp"

#include <functional>
#include <string>
#include <utility>

/*
 * 用语音说一句话（2026-11-16 用户定规：语音由她自己决定，没有手动开关；对方明确要求用语音时
 * 才渲染成语音，平时以文字为主）。两条路径同名同义：
 *   - 对话路径（私聊、群里被 @）：注册表工具，终止本轮、不再另发文字；
 *   - 话题会话轮：定案动词，与 group_say 并列（EngagementService 定案后经 execute 送达）。
 *
 * 开放条件：管理员语音总开关开着（voice.enabled / #激活语音、#冻结语音），且语音服务此刻
 * 可用（Voice::reachable，60 秒缓存的探活）；否则工具表里没有它。
 * 内容与送达分离：合成失败就用文字发出原话，失败永不顶替内容。回灌/入历史的都是原话，
 * 语音入群缓冲也存原话（VoiceMessage.transcript）。语音消息无法引用或 @ 人（协议限制）。
 */
class SendVoiceTool : public Tool
{
public:
    // 合成：成功返回音频文件路径，失败返回空串
    using Synthesize = std::function<std::string(const std::string &text)>;
    // 语音服务此刻是否可用
    using Reachable = std::function<bool()>;

    SendVoiceTool(const Live<bool> &globalVoice, Synthesize synthesize, Reachable reachable)
        : globalVoice(globalVoice), synthesize(std::move(synthesize)), reachable(std::move(reachable))
    {
    }

    std::string name() const override { return KleinSendVoiceToolName; }

    std::string description() const override
    {
        return "把一句话用语音说出来：合成后以语音消息发出，代替这一轮的文字回复。"
               "只在对方明确要求你用语音时调用，例如“用语音回答”“说句话给我听”“发段语音”“念一下”；"
               "对方没有明确要求时，一律用文字回复，不要调用。语音消息不能引用或 @ 任何人。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"text":{"type":"string","description":"要说出口的话本身：口语、简短；不带表情符号、颜文字、Markdown、链接和括号里的动作描写"}},"required":["text"]})";
    }

    bool available() const override
    {
        return *globalVoice.get() && reachable && reachable();
    }

    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &) const override
    {
        return guidanceText(scene);
    }

    // 规则文本（静态，金样测试直接取用）
    static std::string guidanceText(GuidanceScene scene)
    {
        if (scene == GuidanceScene::GroupTurn)
            return "\n\n[系统注] 群里有人明确要求你用语音说话时，用 send_voice 代替 group_say 说这句话；"
                   "没有明确要求就用 group_say。";
        return "只有对方明确要求你用语音说话时才调用 send_voice，把要说的话放进 text；"
               "其余情况都用文字回复。";
    }

    ToolResult execute(const std::string &args, const ToolContext &) override
    {
        std::string text;
        try
        {
            const nlohmann::json arguments = parseToolArguments(args);
            if (arguments.is_object() && arguments.contains("text") && arguments["text"].is_string())
                text = utils::trim(arguments["text"].get<std::string>());
        }
        catch (const std::exception &)
        {
        }
        if (text.empty())
            return {"错误：text 为空，没有可说的话。", {}, {}};

        ToolResult result;
        result.model_content = text; // 终止时它就是这一轮写进历史的回复：原话
        result.terminal = true;
        result.suppress_text_reply = true;
        const std::string audioPath = synthesize ? synthesize(text) : std::string();
        if (!audioPath.empty())
        {
            result.outbound_messages.push_back(VoiceMessage{audioPath, text});
        }
        else
        {
            LOG_WARNING("语音合成失败，本条改发文字");
            result.outbound_messages.push_back(TextMessage{text});
        }
        return result;
    }

private:
    const Live<bool> &globalVoice;
    Synthesize synthesize;
    Reachable reachable;
};

#endif // SEND_VOICE_TOOL_H
