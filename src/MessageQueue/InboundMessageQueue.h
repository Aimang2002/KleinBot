#ifndef INBOUND_MESSAGE_QUEUE_H
#define INBOUND_MESSAGE_QUEUE_H

#include "../Port/InboundMessage.h"

#include <mutex>
#include <optional>
#include <queue>

class InboundMessageQueue
{
public:
    void push(InboundMessage message);
    std::optional<InboundMessage> tryPop();

private:
    std::queue<InboundMessage> queue;
    std::mutex mutex;
};

#endif // INBOUND_MESSAGE_QUEUE_H
