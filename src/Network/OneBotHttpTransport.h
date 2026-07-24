#ifndef ONEBOT_HTTP_TRANSPORT_H
#define ONEBOT_HTTP_TRANSPORT_H

#include "TransportConfig.h"
#include "../MessageQueue/InboundMessageQueue.h"
#include "../MessageQueue/OutboundMessageQueue.h"
#include "../Protocol/OneBot/OneBotEventDecoder.h"
#include "../Protocol/OneBot/OneBotMessageEncoder.h"

#include <atomic>

class OneBotHttpTransport
{
public:
    static void run(
        const TransportConfig &config,
        InboundMessageQueue &inboundQueue,
        OutboundMessageQueue &outboundQueue,
        const OneBotEventDecoder &eventDecoder,
        const OneBotMessageEncoder &messageEncoder,
        const std::atomic<bool> &running);

private:
    static void runApiSender(
        const TransportConfig &config,
        OutboundMessageQueue &outboundQueue,
        const OneBotMessageEncoder &messageEncoder,
        const std::atomic<bool> &transportRunning,
        const std::atomic<bool> &running);

    static void runEventServer(
        const TransportConfig &config,
        InboundMessageQueue &inboundQueue,
        const OneBotEventDecoder &eventDecoder,
        const std::atomic<bool> &running);
};

#endif // ONEBOT_HTTP_TRANSPORT_H
