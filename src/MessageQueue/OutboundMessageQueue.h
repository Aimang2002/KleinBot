#ifndef OUTBOUND_MESSAGE_QUEUE_H
#define OUTBOUND_MESSAGE_QUEUE_H

#include "../Port/OutboundDelivery.h"

#include <mutex>
#include <optional>
#include <queue>

class OutboundMessageQueue
{
public:
    void push(OutboundDelivery delivery);
    std::optional<OutboundDelivery> tryPop();

private:
    std::queue<OutboundDelivery> queue;
    std::mutex mutex;
};

#endif // OUTBOUND_MESSAGE_QUEUE_H
