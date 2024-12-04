#include "MyWebSocket.h"

void MyWebSocket::connectWebSocket(const std::string _url = "/")
{
    // 设置服务器的IP地址和端口
    std::string host = CManager.configVariable("WEBSOCKET_MESSAGE_IP");
    std::string port = CManager.configVariable("WEBSOCKET_MESSAGE_PORT");

    while (true)
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
            ws.handshake(host, _url.c_str());

            LOG_INFO("正向WebSocket连接成功！");

            // 持续监听消息
            while (true)
            {
                // 准备接收消息的缓冲区
                multi_buffer buffer;

                // 读取消息到缓冲区
                ws.read(buffer);

                // 将数据放到string中
                std::string message = boost::beast::buffers_to_string(buffer.data());
#ifdef DEBUG
                LOG_INFO("原始数据：" + message);
#endif

                // 消息类型过滤
                if (!filterMessageType(message))
                {
                    // 加入消息队列
                    MessageQueue::original_push_queue(message);
                }
                else
                {
#ifdef DEBUG
                    LOG_WARNING("过滤消息");
#endif
                }
            }
        }
        catch (std::exception const &e)
        {
            LOG_ERROR(e.what());
        }

        LOG_FATAL("正向ws已失联，5秒后将重新连接...");
        std::this_thread::sleep_for(std::chrono::seconds(5)); // 休眠5秒后重连
    }
}

bool MyWebSocket::filterMessageType(std::string originalMessage)
{
    // 过滤非消息事件
    if (originalMessage.find(R"("post_type":"message")") == originalMessage.npos)
    {
        return true;
    }
    return false;
}