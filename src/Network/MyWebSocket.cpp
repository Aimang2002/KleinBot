#include "MyWebSocket.h"
#include "../utils/Utils.hpp"

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

void MyWebSocket::connectWebSocket(const std::string &url, const std::atomic<bool> &running)
{
    // 设置服务器的IP地址和端口
    std::string host = ConfigManager::getInstance().configVariable("WEBSOCKET_MESSAGE_IP");
    std::string port = ConfigManager::getInstance().configVariable("WEBSOCKET_MESSAGE_PORT");

    while (running.load())
    {
        try
        {
            // 创建IO上下文
            io_context ioc;

            // 从IO上下文创建WebSocket流
            websocket::stream<tcp::socket> ws(ioc);

            // 解析服务器地址和端口
            tcp::resolver resolver(ioc);
            auto const results = resolver.resolve(host, port);

            // 连接到服务器
            connect(ws.next_layer(), results.begin(), results.end());

            // 握手以升级到WebSocket连接
            ws.handshake(host, url);

            LOG_INFO("正向WebSocket连接成功！");

            while (running.load())
            {
                multi_buffer buffer;
                beast::error_code readError;
                bool readCompleted = false;

                ws.async_read(buffer, [&](beast::error_code error, std::size_t) {
                    readError = error;
                    readCompleted = true;
                });

                while (running.load() && !readCompleted)
                {
                    ioc.run_for(std::chrono::milliseconds(100));
                    ioc.restart();
                }

                if (!running.load())
                {
                    beast::error_code ignoredError;
                    ws.next_layer().cancel(ignoredError);
                    ioc.run();
                    ws.next_layer().close(ignoredError);
                    break;
                }

                if (readError)
                {
                    throw beast::system_error(readError);
                }

                std::string message = boost::beast::buffers_to_string(buffer.data());
#ifdef DEBUG
                LOG_DEBUG("原始数据：" + message);
#endif

                // 消息类型过滤
                if (!utils::Noise_intercept(message))
                {
                    // 加入消息队列
                    MessageQueue::original_push_queue(message);
                }
                else
                {
#ifdef DEBUG
                    LOG_INFO("过滤消息");
#endif
                }
            }
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
