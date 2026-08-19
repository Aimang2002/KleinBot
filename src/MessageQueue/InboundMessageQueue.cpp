#include "InboundMessageQueue.h"

#include <utility>

void InboundMessageQueue::push(InboundMessage message)
{
    std::lock_guard<std::mutex> lock(mutex);
    queue.push(std::move(message));
}

std::optional<InboundMessage> InboundMessageQueue::tryPop()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (queue.empty())
    {
        return std::nullopt;
    }

    InboundMessage message = std::move(queue.front());
    queue.pop();
    return message;
}
