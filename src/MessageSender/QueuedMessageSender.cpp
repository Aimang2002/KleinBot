#include "QueuedMessageSender.h"

QueuedMessageSender::QueuedMessageSender(OutboundMessageQueue &queue)
    : queue(queue)
{
}

void QueuedMessageSender::deliver(OutboundDelivery delivery)
{
    queue.push(std::move(delivery));
}
