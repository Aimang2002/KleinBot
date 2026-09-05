#ifndef QUEUED_MESSAGE_SENDER_H
#define QUEUED_MESSAGE_SENDER_H

#include "../MessageQueue/OutboundMessageQueue.h"
#include "../Port/MessageSenderPort.h"

class QueuedMessageSender : public MessageSenderPort
{
public:
    explicit QueuedMessageSender(OutboundMessageQueue &queue);

    void deliver(OutboundDelivery delivery) override;

private:
    OutboundMessageQueue &queue;
};

#endif // QUEUED_MESSAGE_SENDER_H
