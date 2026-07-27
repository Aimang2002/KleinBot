#include <gtest/gtest.h>

#include "Network/CurlRequestControl.h"
#include "ModelApiCaller/AnthropicStandard/AnthropicStandard.h"
#include "ModelApiCaller/OpenAIStandard/OpenAIStandard.h"

#include <boost/asio.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <curl/curl.h>
#include <future>
#include <mutex>
#include <thread>

namespace
{
size_t discardResponse(char *, size_t size, size_t count, void *)
{
    return size * count;
}
}

TEST(CurlRequestControlTest, CancelsConnectedRequestWhenApplicationStops)
{
    using boost::asio::ip::tcp;

    boost::asio::io_context ioContext;
    tcp::acceptor acceptor(ioContext, tcp::endpoint(tcp::v4(), 0));
    const unsigned short port = acceptor.local_endpoint().port();
    std::promise<void> requestReceived;
    std::mutex serverMutex;
    std::condition_variable serverChanged;
    bool releaseServer = false;

    std::thread server([&]() {
        tcp::socket socket(ioContext);
        acceptor.accept(socket);
        boost::asio::streambuf request;
        boost::asio::read_until(socket, request, "\r\n\r\n");
        requestReceived.set_value();

        std::unique_lock<std::mutex> lock(serverMutex);
        serverChanged.wait(lock, [&]() { return releaseServer; });
    });

    std::atomic<bool> running{true};
    auto request = std::async(std::launch::async, [&]() {
        CURL *curl = curl_easy_init();
        if (curl == nullptr)
            return CURLE_FAILED_INIT;

        const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/blocked";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardResponse);
        CurlRequestControl::configure(curl, &running, 1000, 10000);
        const CURLcode result = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        return result;
    });

    const auto received = requestReceived.get_future().wait_for(std::chrono::seconds(2));
    if (received == std::future_status::ready)
        running.store(false);

    const bool cancelledPromptly =
        request.wait_for(std::chrono::seconds(3)) == std::future_status::ready;

    {
        std::lock_guard<std::mutex> lock(serverMutex);
        releaseServer = true;
    }
    serverChanged.notify_all();
    server.join();

    ASSERT_EQ(received, std::future_status::ready);
    ASSERT_TRUE(cancelledPromptly);
    EXPECT_EQ(request.get(), CURLE_ABORTED_BY_CALLBACK);
}

TEST(ModelCancellationTest, SkipsAllAdapterRequestsAfterApplicationStops)
{
    std::atomic<bool> running{false};
    ChatModel model;
    ChatRequest request;
    OpenAIStandard openAI({}, &running);
    AnthropicStandard anthropic({}, &running);

    EXPECT_TRUE(openAI.request_chat(model, "model", request).cancelled);
    EXPECT_TRUE(openAI.request_vision(model, "model", "question", "base64").cancelled);
    EXPECT_TRUE(openAI.request_image(model, "model", "prompt").cancelled);
    EXPECT_TRUE(anthropic.request_chat(model, "model", request).cancelled);
    EXPECT_TRUE(anthropic.request_vision(model, "model", "question", "base64").cancelled);
    EXPECT_TRUE(anthropic.request_image(model, "model", "prompt").cancelled);
}

TEST(CurlRequestControlTest, MapsTransportFailuresToUserFacingErrors)
{
    EXPECT_EQ(CurlRequestControl::failureStatusCode(CURLE_OPERATION_TIMEDOUT), 504);
    EXPECT_STREQ(CurlRequestControl::failureMessage(CURLE_OPERATION_TIMEDOUT),
                 "模型请求超时，请稍后重试。");
    EXPECT_EQ(CurlRequestControl::failureStatusCode(CURLE_COULDNT_CONNECT), 503);
    EXPECT_STREQ(CurlRequestControl::failureMessage(CURLE_COULDNT_CONNECT),
                 "无法连接模型服务，请稍后重试。");
}

TEST(ModelFailureTest, ReturnsConfigurationErrorInsteadOfParsingEmptyResponse)
{
    std::atomic<bool> running{true};
    ChatModel model;
    ChatRequest request;
    OpenAIStandard openAI({}, &running);
    AnthropicStandard anthropic({}, &running);

    const ChatResponse openAIResponse = openAI.request_chat(model, "model", request);
    EXPECT_EQ(openAIResponse.code, 500);
    EXPECT_EQ(openAIResponse.error_message, "模型服务配置不完整，请联系管理员。");

    const ChatResponse anthropicResponse = anthropic.request_chat(model, "model", request);
    EXPECT_EQ(anthropicResponse.code, 500);
    EXPECT_EQ(anthropicResponse.error_message, "模型服务配置不完整，请联系管理员。");
}
