#include "OneBotWebSocketSession.h"
#include "VoiceAttachmentCleanup.h"

#include <chrono>
#include <optional>

void runOneBotWebSocketSession(
    websocket::stream<tcp::socket> &webSocket,
    net::io_context &ioContext,
    InboundMessageQueue &inboundQueue,
    OutboundMessageQueue &outboundQueue,
    WebSocketApiChannel &apiChannel,
    DeliveryReceiptRegistry &receipts,
    const OneBotEventDecoder &eventDecoder,
    const OneBotMessageEncoder &messageEncoder,
    const std::atomic<bool> &running)
{
    beast::multi_buffer readBuffer;
    beast::error_code readError;
    bool readCompleted = false;
    bool readPending = false;

    beast::error_code writeError;
    bool writeCompleted = false;
    bool writePending = false;
    std::optional<std::string> writePayload;
    // 与 writePayload 一一对应：本次待写投递的语音附件，写入落定（成功或失败）后删除
    VoiceAttachmentCleanup voiceCleanup;
    // 回执超时清扫节流：循环每 50ms 一拍，清扫每秒一次足够
    auto lastReceiptSweep = std::chrono::steady_clock::now();

    auto startRead = [&]() {
        readCompleted = false;
        readError = {};
        readPending = true;
        webSocket.async_read(readBuffer, [&](beast::error_code error, std::size_t) {
            readError = error;
            readCompleted = true;
            readPending = false;
        });
    };

    try
    {
        startRead();
        while (running.load())
        {
            if (!writePending)
            {
                // API 调用优先于业务投递：有调用方在同步等响应
                OneBotAction outgoingAction;
                if (apiChannel.tryPopAction(outgoingAction))
                {
                    writePayload = outgoingAction.toJson().dump();
                }
                else if (auto delivery = outboundQueue.tryPop())
                {
                    voiceCleanup.hold(voiceAttachmentPath(delivery->message));
                    OneBotAction action = messageEncoder.encode(*delivery);
                    // 要回执的投递带 echo（R0.2）：响应帧由 receipts 兑现；
                    // 无 correlation 的投递与历史完全一致（不带 echo）
                    if (!delivery->correlation_id.empty())
                        action.echo = receipts.track(delivery->correlation_id);
                    writePayload = action.toJson().dump();
                }

                if (writePayload)
                {
                    writeCompleted = false;
                    writeError = {};
                    writePending = true;
                    webSocket.text(true);
                    webSocket.async_write(net::buffer(*writePayload),
                        [&](beast::error_code error, std::size_t) {
                            writeError = error;
                            writeCompleted = true;
                            writePending = false;
                        });
                }
            }

            ioContext.run_for(std::chrono::milliseconds(50));
            ioContext.restart();

            if (std::chrono::steady_clock::now() - lastReceiptSweep >= std::chrono::seconds(1))
            {
                receipts.sweep();
                lastReceiptSweep = std::chrono::steady_clock::now();
            }

            if (writeCompleted)
            {
                writeCompleted = false;
                writePayload.reset();
                voiceCleanup.cleanup();
                if (writeError)
                {
                    throw beast::system_error(writeError);
                }
            }

            if (readCompleted)
            {
                readCompleted = false;
                if (readError)
                {
                    throw beast::system_error(readError);
                }

                const std::string payload = beast::buffers_to_string(readBuffer.data());
                readBuffer.consume(readBuffer.size());
                try
                {
                    if (auto result = eventDecoder.decodeResponse(payload))
                    {
                        // echo 空间分段：业务投递回执（高段）先问 receipts，
                        // 未命中才是 API 调用（低段）
                        if (!receipts.resolve(*result))
                            apiChannel.resolve(std::move(*result));
                    }
                    else if (auto event = eventDecoder.decode(payload))
                    {
                        inboundQueue.push(std::move(*event));
                    }
                }
                catch (const std::exception &error)
                {
                    LOG_ERROR("OneBot WebSocket数据解析失败：" + std::string(error.what()));
                }

                startRead();
            }
        }
    }
    catch (...)
    {
        // 所有未决 API 调用按失败兑现，调用方不会悬挂到超时；
        // 未决投递回执按 unknown 兑现（断连不等于没发出去）
        apiChannel.failAll("WebSocket 会话异常断开");
        receipts.failAll("WebSocket 会话异常断开");
        throw;
    }

    apiChannel.failAll("WebSocket 会话已结束");
    receipts.failAll("WebSocket 会话已结束");

    beast::error_code ignoredError;
    webSocket.next_layer().cancel(ignoredError);
    ioContext.run();
    webSocket.next_layer().close(ignoredError);
}
