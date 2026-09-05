#ifndef OUTBOUND_DELIVERY_H
#define OUTBOUND_DELIVERY_H

#include "OutboundMessage.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

struct DirectMessageTarget
{
    std::string user_id;
};

struct GroupMessageTarget
{
    std::string group_id;
};

using OutboundTarget = std::variant<DirectMessageTarget, GroupMessageTarget>;

// 回应上下文：描述"这条消息回应谁"，与消息内容无关。
// 由协议边缘适配器翻译成具体段（OneBot 为 reply + at 段；
// Milky/Satori 同样有引用语义），业务层不接触 CQ 码
struct ReplyContext
{
    std::int64_t message_id = 0; // >0 才编码引用段；部分实现端消息 ID 为字符串
    std::string message_id_raw;  // 非空时优先于 message_id（字符串形态原样回填）
    std::string at_user_id;      // 非空才编码 @ 段
};

struct OutboundDelivery
{
    OutboundTarget target;
    OutboundMessage message;
    std::optional<ReplyContext> reply;
};

#endif // OUTBOUND_DELIVERY_H
