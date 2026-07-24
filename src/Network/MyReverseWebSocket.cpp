#include "MyReverseWebSocket.h"
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

void MyReverseWebSocket::connectReverseWebSocket(
    const ReverseWebSocketConfig &config,
    InboundMessageQueue &inboundQueue,
    OutboundMessageQueue &outboundQueue,
    const OneBotEventDecoder &eventDecoder,
    const OneBotMessageEncoder &messageEncoder,
    const std::atomic<bool> &running)
{
    while (running.load())
    {
        try
        {
            net::io_context ioc{1};
            tcp::acceptor acceptor{ioc};
            acceptor.open(tcp::v4());
            acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor.bind({net::ip::make_address(config.bindHost), config.bindPort});
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
            if (!BearerAuth::isAuthorized(
                    std::string_view(authorization.data(), authorization.size()), config.authToken))
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

            if (request.target() != config.path)
            {
                beast::http::response<beast::http::empty_body> response{
                    beast::http::status::not_found, request.version()};
                response.set(beast::http::field::server, "KleinBot");
                response.keep_alive(false);
                beast::http::write(socket, response);
                beast::error_code ignoredError;
                socket.close(ignoredError);
                continue;
            }

            websocket::stream<tcp::socket> ws{std::move(socket)};
            ws.accept(request);

            LOG_INFO("反向WebSocket连接成功!");

            runOneBotWebSocketSession(
                ws, ioc, inboundQueue, outboundQueue, eventDecoder, messageEncoder, running);
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
