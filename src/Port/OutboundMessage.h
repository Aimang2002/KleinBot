#ifndef OUTBOUND_MESSAGE_H
#define OUTBOUND_MESSAGE_H

#include <optional>
#include <string>
#include <variant>

// 纯文本消息：90% 命令 + LLM 普通对话
struct TextMessage
{
    std::string content;
};

// 图片消息：base64 / URL / 本地文件路径
// 用 enum 显式区分来源，由协议边缘适配器决定最终编码方式
// 避免业务层泄漏 "base64://" 这类协议细节
struct ImageMessage
{
    enum class Source
    {
        Base64,
        Url,
        LocalPath
    };

    Source source;
    std::string data;
};

// 音乐分享：当前只用网易云，platform 字段先 YAGNI
struct MusicMessage
{
    long long song_id;
};

// 语音消息：GPT-SoVITS 生成后给的本地音频路径
struct VoiceMessage
{
    std::string audio_path;
};

// 命令 / LLM 编排侧返回的统一类型，保持独立于 OneBot、Milky、Satori 等协议
using OutboundMessage = std::variant<
    TextMessage,
    ImageMessage,
    MusicMessage,
    VoiceMessage>;

// 消息携带语音附件时返回其本地路径，供 transport 在发送落定后清理临时文件
inline std::optional<std::string> voiceAttachmentPath(const OutboundMessage &message)
{
    if (const auto *voice = std::get_if<VoiceMessage>(&message))
        return voice->audio_path;
    return std::nullopt;
}

#endif // OUTBOUND_MESSAGE_H
