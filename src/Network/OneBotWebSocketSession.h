#ifndef ONEBOT_WEBSOCKET_SESSION_H
#define ONEBOT_WEBSOCKET_SESSION_H

#include "WebSocketHead.h"
#include "WebSocketApiChannel.h"
#include "../MessageQueue/InboundMessageQueue.h"
#include "../MessageQueue/OutboundMessageQueue.h"
#include "../Protocol/OneBot/OneBotEventDecoder.h"
#include "../Protocol/OneBot/OneBotMessageEncoder.h"

#include <atomic>

void runOneBotWebSocketSession(
    websocket::stream<tcp::socket> &webSocket,
    net::io_context &ioContext,
    InboundMessageQueue &inboundQueue,
    OutboundMessageQueue &outboundQueue,
    WebSocketApiChannel &apiChannel,
    const OneBotEventDecoder &eventDecoder,
    const OneBotMessageEncoder &messageEncoder,
    const std::atomic<bool> &running);

#endif // ONEBOT_WEBSOCKET_SESSION_H
