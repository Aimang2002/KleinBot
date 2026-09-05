#include "MyWebSocket.h"
#include "OneBotWebSocketSession.h"
#include "BearerAuth.h"

namespace
{
bool waitForReconnect(const std::atomic<bool> &running, std::chrono::seconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (running.load() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return running.load();
}
}

void MyWebSocket::connectWebSocket(
    const ForwardWebSocketConfig &config,
    InboundMessageQueue &inboundQueue,
    OutboundMessageQueue &outboundQueue,
    WebSocketApiChannel &apiChannel,
    const OneBotEventDecoder &eventDecoder,
    const OneBotMessageEncoder &messageEncoder,
    const std::atomic<bool> &running)
{
    while (running.load())
    {
        try
        {
            // 创建IO上下文
            io_context ioc;

            // 从IO上下文创建WebSocket流
            websocket::stream<tcp::socket> ws(ioc);
            if (!config.authToken.empty())
            {
                const std::string authorizationValue = BearerAuth::buildAuthorizationValue(config.authToken);
                ws.set_option(websocket::stream_base::decorator(
                    [authorizationValue](websocket::request_type &request) {
                        request.set(beast::http::field::authorization, authorizationValue);
                    }));
            }

            // 解析服务器地址和端口
            tcp::resolver resolver(ioc);
            auto const results = resolver.resolve(config.host, config.port);

            // 连接到服务器
            connect(ws.next_layer(), results.begin(), results.end());

            // 握手以升级到WebSocket连接
            ws.handshake(config.host + ":" + config.port, config.path);

            LOG_INFO("正向WebSocket连接成功！");

            runOneBotWebSocketSession(
                ws, ioc, inboundQueue, outboundQueue, apiChannel, eventDecoder, messageEncoder, running);
        }
        catch (std::exception const &e)
        {
            if (running.load())
            {
                LOG_ERROR(e.what());
            }
        }

        if (running.load())
        {
            LOG_FATAL("正向ws已失联，5秒后将重新连接...");
            waitForReconnect(running, std::chrono::seconds(5));
        }
    }

    LOG_INFO("正向WebSocket线程已退出");
}
