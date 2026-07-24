#include "QueuedMessageSender.h"

QueuedMessageSender::QueuedMessageSender(OutboundMessageQueue &queue)
    : queue(queue)
{
}

void QueuedMessageSender::send_private(long long user_id, const OutboundMessage &message)
{
    queue.push(OutboundDelivery{DirectMessageTarget{std::to_string(user_id)}, message});
}

void QueuedMessageSender::send_group(long long group_id, const OutboundMessage &message)
{
    queue.push(OutboundDelivery{GroupMessageTarget{std::to_string(group_id)}, message});
}
