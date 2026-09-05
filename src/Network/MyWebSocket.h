#ifndef MYWEBSOCKET_H
#define MYWEBSOCKET_H
#include "WebSocketHead.h"
#include "TransportConfig.h"
#include "WebSocketApiChannel.h"
#include "../MessageQueue/InboundMessageQueue.h"
#include "../MessageQueue/OutboundMessageQueue.h"
#include "../Protocol/OneBot/OneBotEventDecoder.h"
#include "../Protocol/OneBot/OneBotMessageEncoder.h"
#include <atomic>

class MyWebSocket
{
public:
    static void connectWebSocket(
        const ForwardWebSocketConfig &config,
        InboundMessageQueue &inboundQueue,
        OutboundMessageQueue &outboundQueue,
        WebSocketApiChannel &apiChannel,
        const OneBotEventDecoder &eventDecoder,
        const OneBotMessageEncoder &messageEncoder,
        const std::atomic<bool> &running);
};

#endif // MYWEBSOCKET_H
