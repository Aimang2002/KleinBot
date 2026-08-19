#include "OutboundMessageQueue.h"

void OutboundMessageQueue::push(OutboundDelivery delivery)
{
    std::lock_guard<std::mutex> lock(mutex);
    queue.push(std::move(delivery));
}

std::optional<OutboundDelivery> OutboundMessageQueue::tryPop()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (queue.empty())
    {
        return std::nullopt;
    }

    OutboundDelivery delivery = std::move(queue.front());
    queue.pop();
    return delivery;
}
