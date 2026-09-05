#ifndef MESSAGE_SENDER_PORT_H
#define MESSAGE_SENDER_PORT_H

#include "OutboundDelivery.h"

// 出站端口：业务层把语义化投递交给它，由具体适配器翻译成协议格式发出。
// 单一 deliver 入口：私聊/群聊由 target 变体携带，回应元数据（引用+@）由
// delivery.reply 携带——两者都是"这条消息怎么发"的一部分，不宜拆成方法族
class MessageSenderPort
{
public:
    virtual ~MessageSenderPort() = default;

    virtual void deliver(OutboundDelivery delivery) = 0;
};

#endif // MESSAGE_SENDER_PORT_H
