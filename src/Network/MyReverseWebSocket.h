#ifndef MYREVERSEWEBSOCKET_H
#define MYREVERSEWEBSOCKET_H
#include "WebSocketHead.h"
#include "TransportConfig.h"
#include "../MessageQueue/InboundMessageQueue.h"
#include "../MessageQueue/OutboundMessageQueue.h"
#include "../Protocol/OneBot/OneBotEventDecoder.h"
#include "../Protocol/OneBot/OneBotMessageEncoder.h"
#include <atomic>

class MyReverseWebSocket
{
public:
    static void connectReverseWebSocket(
        const ReverseWebSocketConfig &config,
        InboundMessageQueue &inboundQueue,
        OutboundMessageQueue &outboundQueue,
        const OneBotEventDecoder &eventDecoder,
        const OneBotMessageEncoder &messageEncoder,
        const std::atomic<bool> &running);

private:
};

#endif // REVERSEWEBSOCKET_H
