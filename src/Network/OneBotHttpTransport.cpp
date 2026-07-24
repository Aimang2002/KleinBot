#include "OneBotHttpTransport.h"

#include "BearerAuth.h"
#include "WebSocketHead.h"
#include "../Log/Log.h"

#include <curl/curl.h>

#include <chrono>
#include <memory>
#include <thread>

namespace
{
struct HttpCancellationState
{
    const std::atomic<bool> &transportRunning;
    const std::atomic<bool> &applicationRunning;
};

size_t appendResponse(char *data, size_t size, size_t count, void *output)
{
    auto *response = static_cast<std::string *>(output);
    response->append(data, size * count);
    return size * count;
}

int cancelWhenStopped(void *runningPointer, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const auto *state = static_cast<const HttpCancellationState *>(runningPointer);
    return state->transportRunning.load() && state->applicationRunning.load() ? 0 : 1;
}

std::string actionUrl(const std::string &baseUrl, const std::string &action)
{
    if (!baseUrl.empty() && baseUrl.back() == '/')
    {
        return baseUrl + action;
    }
    return baseUrl + "/" + action;
}

void sendHttpResponse(
    beast::tcp_stream &stream,
    unsigned version,
    beast::http::status status,
    const std::string &message = "")
{
    beast::http::response<beast::http::string_body> response{status, version};
    response.set(beast::http::field::server, "KleinBot");
    response.keep_alive(false);
    response.body() = message;
    response.prepare_payload();
    beast::error_code ignoredError;
    beast::http::write(stream, response, ignoredError);
}
}

void OneBotHttpTransport::run(
    const TransportConfig &config,
    InboundMessageQueue &inboundQueue,
    OutboundMessageQueue &outboundQueue,
    const OneBotEventDecoder &eventDecoder,
    const OneBotMessageEncoder &messageEncoder,
    const std::atomic<bool> &running)
{
    std::atomic<bool> transportRunning{true};
    std::thread apiThread(
        runApiSender, std::cref(config), std::ref(outboundQueue),
        std::cref(messageEncoder), std::cref(transportRunning), std::cref(running));

    runEventServer(config, inboundQueue, eventDecoder, running);
    transportRunning.store(false);

    if (apiThread.joinable())
    {
        apiThread.join();
    }
    LOG_INFO("OneBot HTTP通信线程已退出");
}

void OneBotHttpTransport::runApiSender(
    const TransportConfig &config,
    OutboundMessageQueue &outboundQueue,
    const OneBotMessageEncoder &messageEncoder,
    const std::atomic<bool> &transportRunning,
    const std::atomic<bool> &running)
{
    HttpCancellationState cancellationState{transportRunning, running};
    while (transportRunning.load() && running.load())
    {
        auto delivery = outboundQueue.tryPop();
        if (!delivery)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        try
        {
            const OneBotAction action = messageEncoder.encode(*delivery);
            const std::string requestBody = action.params.dump();
            std::string responseBody;

            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
            if (!curl)
            {
                LOG_ERROR("OneBot HTTP API初始化失败");
                continue;
            }

            curl_slist *rawHeaders = nullptr;
            rawHeaders = curl_slist_append(rawHeaders, "Content-Type: application/json");
            if (!config.http.apiAuthToken.empty())
            {
                const std::string authorization =
                    "Authorization: " + BearerAuth::buildAuthorizationValue(config.http.apiAuthToken);
                rawHeaders = curl_slist_append(rawHeaders, authorization.c_str());
            }
            std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
                rawHeaders, curl_slist_free_all);

            const std::string url = actionUrl(config.http.apiBaseUrl, action.action);
            curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
            curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, requestBody.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, requestBody.size());
            curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, config.connectTimeoutMs);
            curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, config.requestTimeoutMs);
            curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, appendResponse);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &responseBody);
            curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, cancelWhenStopped);
            curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &cancellationState);

            const CURLcode result = curl_easy_perform(curl.get());
            if (result != CURLE_OK)
            {
                if (running.load())
                {
                    LOG_ERROR("OneBot HTTP API请求失败：" + std::string(curl_easy_strerror(result)));
                }
                continue;
            }

            long statusCode = 0;
            curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &statusCode);
            if (statusCode < 200 || statusCode >= 300)
            {
                LOG_ERROR("OneBot HTTP API返回状态码：" + std::to_string(statusCode));
                continue;
            }

            if (!responseBody.empty())
            {
                const nlohmann::json response = nlohmann::json::parse(responseBody);
                if (response.value("status", "ok") != "ok" || response.value("retcode", 0) != 0)
                {
                    LOG_ERROR("OneBot HTTP API返回失败，retcode=" +
                              std::to_string(response.value("retcode", -1)));
                }
            }
        }
        catch (const std::exception &error)
        {
            LOG_ERROR("OneBot HTTP API处理失败：" + std::string(error.what()));
        }
    }

    LOG_INFO("OneBot HTTP API发送线程已退出");
}

void OneBotHttpTransport::runEventServer(
    const TransportConfig &config,
    InboundMessageQueue &inboundQueue,
    const OneBotEventDecoder &eventDecoder,
    const std::atomic<bool> &running)
{
    try
    {
        net::io_context ioContext{1};
        tcp::acceptor acceptor{ioContext};
        acceptor.open(tcp::v4());
        acceptor.set_option(tcp::acceptor::reuse_address(true));
        acceptor.bind({net::ip::make_address(config.http.eventBindHost), config.http.eventBindPort});
        acceptor.listen();
        LOG_INFO("OneBot HTTP事件服务已启动");

        while (running.load())
        {
            tcp::socket socket{ioContext};
            beast::error_code acceptError;
            bool acceptCompleted = false;
            acceptor.async_accept(socket, [&](beast::error_code error) {
                acceptError = error;
                acceptCompleted = true;
            });

            while (running.load() && !acceptCompleted)
            {
                ioContext.run_for(std::chrono::milliseconds(50));
                ioContext.restart();
            }
            if (!running.load())
            {
                beast::error_code ignoredError;
                acceptor.cancel(ignoredError);
                ioContext.run();
                break;
            }
            if (acceptError)
            {
                throw beast::system_error(acceptError);
            }

            beast::tcp_stream stream{std::move(socket)};
            stream.expires_after(std::chrono::milliseconds(config.requestTimeoutMs));
            beast::flat_buffer buffer;
            beast::http::request_parser<beast::http::string_body> parser;
            parser.body_limit(config.maxBodyBytes);
            beast::error_code readError;
            bool readCompleted = false;
            beast::http::async_read(stream, buffer, parser,
                [&](beast::error_code error, std::size_t) {
                    readError = error;
                    readCompleted = true;
                });

            while (running.load() && !readCompleted)
            {
                ioContext.run_for(std::chrono::milliseconds(50));
                ioContext.restart();
            }
            if (!running.load())
            {
                beast::error_code ignoredError;
                stream.socket().cancel(ignoredError);
                ioContext.run();
                stream.socket().close(ignoredError);
                break;
            }

            if (readError)
            {
                const auto status = readError == beast::http::error::body_limit
                    ? beast::http::status::payload_too_large
                    : beast::http::status::bad_request;
                sendHttpResponse(stream, 11, status);
                continue;
            }

            const auto request = parser.release();
            if (request.method() != beast::http::verb::post)
            {
                sendHttpResponse(stream, request.version(), beast::http::status::method_not_allowed);
                continue;
            }
            if (request.target() != config.http.eventPath)
            {
                sendHttpResponse(stream, request.version(), beast::http::status::not_found);
                continue;
            }

            const auto contentType = request[beast::http::field::content_type];
            if (contentType.substr(0, std::string_view("application/json").size()) != "application/json")
            {
                sendHttpResponse(stream, request.version(), beast::http::status::bad_request);
                continue;
            }

            const auto authorization = request[beast::http::field::authorization];
            if (!BearerAuth::isAuthorized(
                    std::string_view(authorization.data(), authorization.size()),
                    config.http.eventAuthToken))
            {
                sendHttpResponse(stream, request.version(), beast::http::status::unauthorized);
                LOG_WARNING("OneBot HTTP事件认证失败，已拒绝请求");
                continue;
            }

            try
            {
                const nlohmann::json payload = nlohmann::json::parse(request.body());
                if (!payload.is_object() || !payload.contains("post_type"))
                {
                    sendHttpResponse(stream, request.version(), beast::http::status::bad_request);
                    continue;
                }

                if (auto event = eventDecoder.decode(request.body()))
                {
                    inboundQueue.push(std::move(*event));
                }
                sendHttpResponse(stream, request.version(), beast::http::status::no_content);
            }
            catch (const std::exception &)
            {
                sendHttpResponse(stream, request.version(), beast::http::status::bad_request);
            }
        }
    }
    catch (const std::exception &error)
    {
        if (running.load())
        {
            LOG_ERROR("OneBot HTTP事件服务异常：" + std::string(error.what()));
        }
    }

    LOG_INFO("OneBot HTTP事件服务线程已退出");
}
