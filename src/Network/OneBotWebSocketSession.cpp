#include "OneBotWebSocketSession.h"

#include <optional>

void runOneBotWebSocketSession(
    websocket::stream<tcp::socket> &webSocket,
    net::io_context &ioContext,
    InboundMessageQueue &inboundQueue,
    OutboundMessageQueue &outboundQueue,
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

    startRead();
    while (running.load())
    {
        if (!writePending)
        {
            if (auto delivery = outboundQueue.tryPop())
            {
                writePayload = messageEncoder.encode(*delivery).toJson().dump();
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

        if (writeCompleted)
        {
            writeCompleted = false;
            writePayload.reset();
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
                if (auto event = eventDecoder.decode(payload))
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

    beast::error_code ignoredError;
    webSocket.next_layer().cancel(ignoredError);
    ioContext.run();
    webSocket.next_layer().close(ignoredError);
}
