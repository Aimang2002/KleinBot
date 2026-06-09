#ifndef MESSAGE_SENDER_PORT_H
#define MESSAGE_SENDER_PORT_H

#include "OutboundMessage.h"

// 出站端口：业务层把语义化消息交给它，由具体适配器翻译成协议格式发出
//
// 私聊 / 群聊拆成两个方法（而不是统一 send + target 结构体），原因：
//   1. 调用点更直白：sender->send_group(gid, msg) vs sender->send({true, gid}, msg)
//   2. QQ 协议本身就分这两条路径，不强行抽象掉
//   3. 少一个 SendTarget struct 的样板
class MessageSenderPort
{
public:
    virtual ~MessageSenderPort() = default;

    virtual void send_private(long long user_id, const OutboundMessage &msg) = 0;
    virtual void send_group(long long group_id, const OutboundMessage &msg) = 0;
};

#endif // MESSAGE_SENDER_PORT_H
