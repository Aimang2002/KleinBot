#ifndef OUTBOUND_MESSAGE_H
#define OUTBOUND_MESSAGE_H

#include <string>
#include <variant>

// 纯文本消息：90% 命令 + LLM 普通对话
struct TextMessage
{
    std::string content;
};

// 图片消息：base64 / URL / 本地文件路径
// 用 enum 显式区分来源，让 QQMessageSender 自己拼 LLOneBot 的前缀语法
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

// 命令 / LLM 编排侧返回的统一类型
// 协议编码（CQ 码 or 消息段）由 MessageSenderPort 的具体实现负责
using OutboundMessage = std::variant<
    TextMessage,
    ImageMessage,
    MusicMessage,
    VoiceMessage>;

#endif // OUTBOUND_MESSAGE_H
