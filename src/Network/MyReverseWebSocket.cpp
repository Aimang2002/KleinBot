#include "MyReverseWebSocket.h"
#include "WebSocketAuth.h"

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

void MyReverseWebSocket::connectReverseWebSocket(const std::atomic<bool> &running)
{
    unsigned short palpitate = 0; // 心跳机制

    while (running.load())
    {
        try
        {
            std::string const address = ConfigManager::getInstance().configVariable("REVERSEWEBSOCKET_MESSAGE_IP");
            unsigned short const port = stoi(ConfigManager::getInstance().configVariable("REVERSEWEBSOCKET_MESSAGE_PORT"));
            std::string const authToken = ConfigManager::getInstance().configVariableOpt("WEBSOCKET_AUTH_TOKEN");

            net::io_context ioc{1};
            tcp::acceptor acceptor{ioc};
            acceptor.open(tcp::v4());
            acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor.bind({net::ip::make_address(address), port});
            acceptor.listen();

            tcp::socket socket{ioc};
            beast::error_code acceptError;
            bool acceptCompleted = false;
            acceptor.async_accept(socket, [&](beast::error_code error) {
                acceptError = error;
                acceptCompleted = true;
            });

            while (running.load() && !acceptCompleted)
            {
                ioc.run_for(std::chrono::milliseconds(100));
                ioc.restart();
            }

            if (!running.load())
            {
                beast::error_code ignoredError;
                acceptor.cancel(ignoredError);
                ioc.run();
                acceptor.close(ignoredError);
                break;
            }

            if (acceptError)
            {
                throw beast::system_error(acceptError);
            }

            beast::flat_buffer requestBuffer;
            beast::http::request<beast::http::string_body> request;
            beast::error_code requestError;
            bool requestCompleted = false;
            beast::http::async_read(socket, requestBuffer, request,
                [&](beast::error_code error, std::size_t) {
                    requestError = error;
                    requestCompleted = true;
                });

            while (running.load() && !requestCompleted)
            {
                ioc.run_for(std::chrono::milliseconds(100));
                ioc.restart();
            }

            if (!running.load())
            {
                beast::error_code ignoredError;
                socket.cancel(ignoredError);
                ioc.run();
                socket.close(ignoredError);
                break;
            }

            if (requestError)
            {
                throw beast::system_error(requestError);
            }

            const auto authorization = request[beast::http::field::authorization];
            if (!WebSocketAuth::isAuthorized(
                    std::string_view(authorization.data(), authorization.size()), authToken))
            {
                beast::http::response<beast::http::empty_body> response{
                    beast::http::status::unauthorized, request.version()};
                response.set(beast::http::field::server, "KleinBot");
                response.set(beast::http::field::www_authenticate, "Bearer");
                response.keep_alive(false);
                beast::http::write(socket, response);

                beast::error_code ignoredError;
                socket.shutdown(tcp::socket::shutdown_both, ignoredError);
                socket.close(ignoredError);
                LOG_WARNING("反向WebSocket认证失败，已拒绝连接");
                continue;
            }

            websocket::stream<tcp::socket> ws{std::move(socket)};
            ws.accept(request);

            LOG_INFO("反向WebSocket连接成功!");

            while (running.load())
            {
                std::string message;
                if (auto popped = MessageQueue::pending_try_pop()) // 若消息队列不为空
                {
                    message = std::move(*popped);
#ifdef DEBUG
                    LOG_DEBUG("发送数据：" + message);
#endif
                    ws.text(ws.got_text());
                    ws.write(net::buffer(message));
                    std::cout << "send over!" << std::endl;
                }
                else
                {
                    // std::this_thread::sleep_for(std::chrono::seconds(1));        // 休眠一秒
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 休眠100ms
                    palpitate++;
                    if (palpitate > 100) // 当休眠达到十次，将进行一个心跳
                    {
                        ws.text(ws.got_text());
                        ws.write(net::buffer("ping")); // 发送心跳
                        palpitate = 0;
                    }
                }
            }

            beast::error_code ignoredError;
            ws.next_layer().cancel(ignoredError);
            ws.next_layer().close(ignoredError);
        }
        catch (beast::system_error const &se)
        {
            if (running.load())
            {
                LOG_ERROR(se.what());
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
            LOG_FATAL("反向ws已失联，10秒后将重新连接...");
            waitForReconnect(running, std::chrono::seconds(10));
        }
    }

    LOG_INFO("反向WebSocket线程已退出");
}
