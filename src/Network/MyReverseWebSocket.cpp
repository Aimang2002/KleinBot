#include "MyReverseWebSocket.h"

void MyReverseWebSocket::connectReverseWebSocket()
{
    unsigned short palpitate = 0; // 心跳机制

    while (true)
    {
        std::string const address = CManager.configVariable("REVERSEWEBSOCKET_MESSAGE_IP").c_str();
        unsigned short const port = stoi(CManager.configVariable("REVERSEWEBSOCKET_MESSAGE_PORT"));

        net::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {tcp::v4(), port}};
        tcp::socket socket{ioc};

        acceptor.accept(socket);

        try
        {
            websocket::stream<tcp::socket> ws{std::move(socket)};
            ws.accept();

            LOG_INFO("反向WebSocket连接成功!");

            for (;;)
            {
                string message;
                if (!MessageQueue::pending_empty()) // 若消息队列不为空
                {
                    message = MessageQueue::pending_front_queue();
                    MessageQueue::pending_pop();
#ifdef DEBUG
                    cout << "发送数据：" << message << endl;
#endif
                    ws.text(ws.got_text());
                    ws.write(net::buffer(message));
                    cout << "send over!" << endl;
                }
                else
                {
                    sleep(1); // 休眠一秒
                    palpitate++;
                    if (palpitate > 10) // 当休眠达到十次，将进行一个心跳
                    {
                        ws.text(ws.got_text());
                        ws.write(net::buffer("ping")); // 发送心跳
                    }
                }
            }
        }
        catch (beast::system_error const &se)
        {
            std::cerr << "Error: " << se.what() << std::endl;
        }

        LOG_FATAL("反向ws已失联，10秒后将重新连接...");
        sleep(10); // 休眠10后重连
    }
}

std::string MyReverseWebSocket::messageEncapsulation(string message, string messageEndpoint)
{
    string format = R"({"action":")" + messageEndpoint + R"(","params":)" + message + "}"; // \"
    return format;
}