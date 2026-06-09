#ifndef QQ_MESSAGE_SENDER_H
#define QQ_MESSAGE_SENDER_H

#include "../Port/MessageSenderPort.h"
#include "../../Library/nlohmann/json.hpp"

// QQ 协议适配器：把语义化的 OutboundMessage 翻译成 OneBot v11 消息段 JSON
//
// 输出形态采用消息段数组而非 CQ 码字符串：
//   - 零字符串转义负担（[ , & 等不需要 escape）
//   - 不会发生 "文本里恰好有 [CQ: 子串" 的误解析
//   - 用 nlohmann 直接 dump，调试友好
class QQMessageSender : public MessageSenderPort
{
public:
    void send_private(long long user_id, const OutboundMessage &msg) override;
    void send_group(long long group_id, const OutboundMessage &msg) override;

private:
    // OutboundMessage variant → 消息段 JSON 数组
    static nlohmann::json toSegments(const OutboundMessage &msg);

    // 拼 OneBot 外壳：{"action":"send_xxx_msg","params":{...}}
    static std::string buildEnvelope(const std::string &action, const nlohmann::json &params);
};

#endif // QQ_MESSAGE_SENDER_H
