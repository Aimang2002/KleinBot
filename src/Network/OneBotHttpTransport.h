#ifndef ONEBOT_HTTP_TRANSPORT_H
#define ONEBOT_HTTP_TRANSPORT_H

#include "TransportConfig.h"
#include "../MessageQueue/InboundMessageQueue.h"
#include "../MessageQueue/OutboundMessageQueue.h"
#include "../Port/DeliveryReceipt.h"
#include "../Protocol/OneBot/OneBotEventDecoder.h"
#include "../Protocol/OneBot/OneBotMessageEncoder.h"

#include <atomic>

class OneBotHttpTransport
{
public:
    // receiptSink 可空：带 correlation_id 的投递按 HTTP 响应体（status/retcode/
    // data.message_id）直接回报 sent/failed，网络层拿不到响应时回报 unknown
    static void run(
        const TransportConfig &config,
        InboundMessageQueue &inboundQueue,
        OutboundMessageQueue &outboundQueue,
        const OneBotEventDecoder &eventDecoder,
        const OneBotMessageEncoder &messageEncoder,
        DeliveryReceiptSink *receiptSink,
        const std::atomic<bool> &running);

private:
    static void runApiSender(
        const TransportConfig &config,
        OutboundMessageQueue &outboundQueue,
        const OneBotMessageEncoder &messageEncoder,
        DeliveryReceiptSink *receiptSink,
        const std::atomic<bool> &transportRunning,
        const std::atomic<bool> &running);

    static void runEventServer(
        const TransportConfig &config,
        InboundMessageQueue &inboundQueue,
        const OneBotEventDecoder &eventDecoder,
        const std::atomic<bool> &running);
};

#endif // ONEBOT_HTTP_TRANSPORT_H
