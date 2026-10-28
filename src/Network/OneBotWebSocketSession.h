#ifndef ONEBOT_WEBSOCKET_SESSION_H
#define ONEBOT_WEBSOCKET_SESSION_H

#include "WebSocketHead.h"
#include "WebSocketApiChannel.h"
#include "DeliveryReceiptRegistry.h"
#include "../MessageQueue/InboundMessageQueue.h"
#include "../MessageQueue/OutboundMessageQueue.h"
#include "../Protocol/OneBot/OneBotEventDecoder.h"
#include "../Protocol/OneBot/OneBotMessageEncoder.h"

#include <atomic>

// 单条 Universal WebSocket 会话循环（服务器模式/客户端模式共用）。
// receipts：带 correlation_id 的业务投递在此分配 echo，响应帧先经它兑现回执，
// 未命中再交给 apiChannel；断连时两者都 failAll
void runOneBotWebSocketSession(
    websocket::stream<tcp::socket> &webSocket,
    net::io_context &ioContext,
    InboundMessageQueue &inboundQueue,
    OutboundMessageQueue &outboundQueue,
    WebSocketApiChannel &apiChannel,
    DeliveryReceiptRegistry &receipts,
    const OneBotEventDecoder &eventDecoder,
    const OneBotMessageEncoder &messageEncoder,
    const std::atomic<bool> &running);

#endif // ONEBOT_WEBSOCKET_SESSION_H
