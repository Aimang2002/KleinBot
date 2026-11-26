#include "MyReverseWebSocket.h"
#include "OneBotWebSocketSession.h"
#include "BearerAuth.h"
#include "../utils/Encoding.h"

#include <boost/system/system_error.hpp>

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
    WebSocketApiChannel &apiChannel,
    DeliveryReceiptRegistry &receipts,
    const OneBotEventDecoder &eventDecoder,
    const OneBotMessageEncoder &messageEncoder,
    const std::atomic<bool> &running)
{
    // 日志里必须带地址：boost 的 "bind: Address already in use" 本身不含端口，
    // 只打 what() 时无法判断是哪个监听口冲突。
    const std::string endpoint =
        config.bindHost + ":" + std::to_string(config.bindPort);

    // 外层一圈 = 一个监听周期。监听器在线程存活期间常驻，只有 bind/listen/accept
    // 本身出错才重建：若随每个连接重建，已排进旧 acceptor 积压队列的连接会在它析构时
    // 被内核 RST（协议端收到 401 立即重连就会撞上），重建前后端口也有一段没人监听。
    while (running.load())
    {
        try
        {
            net::io_context acceptorIoc{1};
            tcp::acceptor acceptor{acceptorIoc};
            acceptor.open(tcp::v4());
            acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor.bind({net::ip::make_address(config.bindHost), config.bindPort});
            acceptor.listen();

            // 内层一圈 = 一个连接；同一时刻只服务一个，会话期间新连接在积压队列里等
            while (running.load())
            {
                // 每个连接一个新的 io_context，连接作用域结束时随之销毁：读请求与会话的
                // 回调都按引用捕获本圈（及会话函数内）的局部变量，连接异常退出时可能留下
                // 已取消但未执行的回调；若与 acceptor 共用长寿命 io_context，它们会在
                // 下一圈 run_for 时对着已销毁的局部变量执行。io_context 析构只销毁未执行的
                // 回调而不调用。socket 属于连接的 io_context，accept 由 acceptor 的
                // io_context 驱动，Asio 允许这样跨执行上下文 accept。
                net::io_context ioc{1};
                tcp::socket socket{ioc};
                beast::error_code acceptError;
                bool acceptCompleted = false;
                acceptor.async_accept(socket, [&](beast::error_code error) {
                    acceptError = error;
                    acceptCompleted = true;
                });

                while (running.load() && !acceptCompleted)
                {
                    acceptorIoc.run_for(std::chrono::milliseconds(100));
                    acceptorIoc.restart();
                }

                if (!running.load())
                {
                    beast::error_code ignoredError;
                    acceptor.cancel(ignoredError);
                    acceptorIoc.run();
                    acceptor.close(ignoredError);
                    break;
                }

                // accept 本身失败说明监听器出了问题，交给外层重建
                if (acceptError)
                {
                    throw beast::system_error(acceptError);
                }

                // 以下任何失败都只结束这一个连接：记日志后立即接下一个，
                // 不销毁 acceptor、不等待
                bool sessionStarted = false;
                auto reportConnectionFailure = [&](const std::string &detail) {
                    if (!running.load())
                    {
                        return;
                    }
                    if (sessionStarted)
                    {
                        LOG_ERROR("WebSocket（客户端模式）会话异常（监听 " + endpoint + "）：" + detail);
                        LOG_FATAL("WebSocket（客户端模式）已失联（监听 " + endpoint +
                                  "），继续监听，等待协议端重新连接...");
                    }
                    else
                    {
                        LOG_WARNING("WebSocket（客户端模式）连接在握手完成前失败，已断开该连接（监听 " +
                                    endpoint + "）：" + detail);
                    }
                };

                try
                {
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
                        LOG_WARNING("WebSocket（客户端模式）认证失败，已拒绝连接");
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
                        LOG_WARNING("WebSocket（客户端模式）请求路径与配置的 " + config.path +
                                    " 不符，已返回 404");
                        continue;
                    }

                    websocket::stream<tcp::socket> ws{std::move(socket)};
                    ws.accept(request);

                    LOG_INFO("WebSocket（客户端模式）连接成功!");
                    sessionStarted = true;

                    runOneBotWebSocketSession(
                        ws, ioc, inboundQueue, outboundQueue, apiChannel, receipts,
                        eventDecoder, messageEncoder, running);

                    // 会话只在停止时正常返回；万一在运行中返回，同样按失联记录
                    if (running.load())
                    {
                        LOG_FATAL("WebSocket（客户端模式）已失联（监听 " + endpoint +
                                  "），继续监听，等待协议端重新连接...");
                    }
                }
                catch (boost::system::system_error const &se)
                {
                    // 系统错误消息是本地 ANSI 编码（中文 Windows 为 GBK），转 UTF-8 后再进日志
                    reportConnectionFailure(utils::localToUtf8(se.what()));
                }
                catch (std::exception const &e)
                {
                    reportConnectionFailure(e.what());
                }
            }
        }
        catch (boost::system::system_error const &se)
        {
            if (running.load())
            {
                // 系统错误消息是本地 ANSI 编码（中文 Windows 为 GBK），转 UTF-8 后再进日志
                const std::string detail = utils::localToUtf8(se.what());
                if (se.code() == boost::asio::error::address_in_use)
                {
                    LOG_ERROR("WebSocket（客户端模式）监听失败：监听地址 " + endpoint +
                              " 已被占用（" + detail +
                              "）。若协议端的服务器模式也使用该地址，请改成不同端口；"
                              "协议端服务器与 Klein 监听不能共用同一地址");
                }
                else
                {
                    LOG_ERROR("WebSocket（客户端模式）监听异常（监听 " + endpoint + "）：" + detail);
                }
            }
        }
        catch (std::exception const &e)
        {
            if (running.load())
            {
                LOG_ERROR("WebSocket（客户端模式）监听异常（监听 " + endpoint + "）：" + e.what());
            }
        }

        // 只有监听本身失败才走到这里（停止时 running 已为 false）：
        // 端口可能仍被占用，等一会儿再重建，避免刷屏
        if (running.load())
        {
            LOG_FATAL("WebSocket（客户端模式）监听已中断（监听 " + endpoint + "），10秒后将重新监听...");
            waitForReconnect(running, std::chrono::seconds(10));
        }
    }

    LOG_INFO("WebSocket（客户端模式）线程已退出");
}
