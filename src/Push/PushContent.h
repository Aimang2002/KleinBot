#ifndef PUSH_CONTENT_H
#define PUSH_CONTENT_H

#include <cstdint>
#include <string>

// 推送内容块：与 MCP tools/call 的 content blocks 同构（type + 数据 + mime），
// 语法只认 type/data，语义（深渊播报还是 Steam 人数）永远住在调用方
struct PushTarget
{
    bool isGroup = false;
    std::int64_t id = 0;
};

struct PushContent
{
    std::string type; // "text" | "image"（阶段2: "voice"）
    std::string data; // text 为原文；image 为 base64
    std::string mime; // image/voice 可选，当前仅透传不参与校验
};

enum class PushReject
{
    Ok,
    DeniedTarget,   // 目标不在白名单
    RateLimited,    // 超出该目标每小时频率帽
    TextTooLarge,   // 单个 text 块超过上限
    ImageTooLarge,  // 单个 image 块超过上限
    UnsupportedType, // 内容类型不在 supported 清单（HTTP 层原样回传给调用方）
    InvalidContent  // 空内容列表或空数据块
};

#endif // PUSH_CONTENT_H
