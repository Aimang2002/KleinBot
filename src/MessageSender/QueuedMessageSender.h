#ifndef QUEUED_MESSAGE_SENDER_H
#define QUEUED_MESSAGE_SENDER_H

#include "../MessageQueue/OutboundMessageQueue.h"
#include "../Port/MessageSenderPort.h"

class QueuedMessageSender : public MessageSenderPort
{
public:
    explicit QueuedMessageSender(OutboundMessageQueue &queue);

    void send_private(long long user_id, const OutboundMessage &message) override;
    void send_group(long long group_id, const OutboundMessage &message) override;

private:
    OutboundMessageQueue &queue;
};

#endif // QUEUED_MESSAGE_SENDER_H
