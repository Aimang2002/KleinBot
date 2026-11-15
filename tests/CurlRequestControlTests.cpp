#include <gtest/gtest.h>

#include "Network/CurlRequestControl.h"
#include "Action/WebFetchAction.h"
#include "ModelApiCaller/AnthropicStandard/AnthropicStandard.h"
#include "ModelApiCaller/OpenAIStandard/OpenAIStandard.h"
#include "WebSearch/TavilySearchProvider.h"
#include "utils/Live.h"

#include <boost/asio.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <curl/curl.h>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
    Live<DockOptions> network{DockOptions{}};
    OpenAIStandard openAI(network, &running);
    AnthropicStandard anthropic(network, &running);

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
    Live<DockOptions> network{DockOptions{}};
    OpenAIStandard openAI(network, &running);
    AnthropicStandard anthropic(network, &running);

    const ChatResponse openAIResponse = openAI.request_chat(model, "model", request);
    EXPECT_EQ(openAIResponse.code, 500);
    EXPECT_EQ(openAIResponse.error_message, "模型服务配置不完整，请联系管理员。");

    const ChatResponse anthropicResponse = anthropic.request_chat(model, "model", request);
    EXPECT_EQ(anthropicResponse.code, 500);
    EXPECT_EQ(anthropicResponse.error_message, "模型服务配置不完整，请联系管理员。");
}

// R1.b 前置：ChatRequest.timeout_ms 一路透传到适配器的 curl 超时。本地服务器只收请求
// 不应答：带 300ms 超时的 chat 请求应在秒级内以"超时"失败（默认 300 秒则会一直挂着）
TEST(ModelTimeoutTest, ChatRequestTimeoutReachesBothAdapters)
{
    using boost::asio::ip::tcp;

    boost::asio::io_context ioContext;
    tcp::acceptor acceptor(ioContext, tcp::endpoint(tcp::v4(), 0));
    const unsigned short port = acceptor.local_endpoint().port();
    std::mutex serverMutex;
    std::condition_variable serverChanged;
    bool releaseServer = false;

    std::thread server([&]() {
        std::vector<tcp::socket> held;
        for (int index = 0; index < 2; ++index)
        {
            tcp::socket socket(ioContext);
            acceptor.accept(socket);
            held.push_back(std::move(socket));
        }
        std::unique_lock<std::mutex> lock(serverMutex);
        serverChanged.wait(lock, [&]() { return releaseServer; });
    });

    std::atomic<bool> running{true};
    ChatModel model;
    model.endpoint = "http://127.0.0.1:" + std::to_string(port) + "/v1/chat";
    model.api_key = "test-key";
    ChatRequest request;
    request.history.push_back({"user", "hi"});
    request.timeout_ms = 300;
    Live<DockOptions> network{DockOptions{}};
    OpenAIStandard openAI(network, &running);
    AnthropicStandard anthropic(network, &running);

    const auto started = std::chrono::steady_clock::now();
    const ChatResponse openAIResponse = openAI.request_chat(model, "model", request);
    const ChatResponse anthropicResponse = anthropic.request_chat(model, "model", request);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    {
        std::lock_guard<std::mutex> lock(serverMutex);
        releaseServer = true;
    }
    serverChanged.notify_all();
    server.join();

    EXPECT_LT(elapsed, std::chrono::seconds(5));
    EXPECT_EQ(openAIResponse.code, 504);
    EXPECT_EQ(anthropicResponse.code, 504);
    EXPECT_EQ(openAIResponse.error_message, "模型请求超时，请稍后重试。");
}

namespace
{
// 本地假服务：记下每个请求的请求行，回一个最小的 200 响应。经 HTTP 代理发出的请求，
// 请求行里是完整网址（"POST http://目标/路径 HTTP/1.1"），据此判断请求经过了哪一个代理
class RequestLineRecorder
{
public:
    explicit RequestLineRecorder(std::string responseBody)
        : acceptor(ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0)),
          responseBody(std::move(responseBody))
    {
        port = acceptor.local_endpoint().port();
        server = std::thread([this]() { serve(); });
    }

    ~RequestLineRecorder()
    {
        stopping = true;
        // 连自己一次，叫醒阻塞的 accept
        boost::system::error_code ignored;
        boost::asio::ip::tcp::socket waker(ioContext);
        waker.connect(acceptor.local_endpoint(), ignored);
        server.join();
    }

    std::vector<std::string> lines() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return requestLines;
    }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port); }

private:
    void serve()
    {
        while (true)
        {
            boost::asio::ip::tcp::socket socket(ioContext);
            boost::system::error_code error;
            acceptor.accept(socket, error);
            if (stopping.load() || error)
                return;
            boost::asio::streambuf buffer;
            const std::size_t headerBytes = boost::asio::read_until(socket, buffer, "\r\n\r\n", error);
            if (error)
                continue;
            const std::string received(boost::asio::buffers_begin(buffer.data()),
                                       boost::asio::buffers_end(buffer.data()));
            const std::string header = received.substr(0, headerBytes);
            {
                std::lock_guard<std::mutex> lock(mutex);
                requestLines.push_back(header.substr(0, header.find("\r\n")));
            }
            // 读完请求体再回应，免得对端写到一半被重置
            std::string lowered = header;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            const std::string marker = "content-length:";
            const std::size_t at = lowered.find(marker);
            const std::size_t contentLength =
                at == std::string::npos ? 0 : std::stoul(header.substr(at + marker.size()));
            const std::size_t bodyBytes = received.size() - headerBytes;
            if (bodyBytes < contentLength)
                boost::asio::read(socket, buffer, boost::asio::transfer_exactly(contentLength - bodyBytes), error);
            const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                         std::to_string(responseBody.size()) +
                                         "\r\nConnection: close\r\n\r\n" + responseBody;
            boost::asio::write(socket, boost::asio::buffer(response), error);
        }
    }

    boost::asio::io_context ioContext;
    boost::asio::ip::tcp::acceptor acceptor;
    std::string responseBody;
    unsigned short port = 0;
    std::atomic<bool> stopping{false};
    std::thread server;
    mutable std::mutex mutex;
    std::vector<std::string> requestLines;
};
}

// 配置实时生效（L1.e）：两个模型适配器、搜索、抓取都在每次请求开头取代理。代理从 A 换成 B
// 之后，四者的下一次请求都经 B 发出。目标地址用 .invalid 域名：只有经代理才发得出去
TEST(ProxyLiveConfigTest, ModelSearchAndFetchUseReplacedProxyOnNextRequest)
{
    const std::string body = R"({"choices":[{"message":{"content":"ok"},"finish_reason":"stop"}],)"
                             R"("content":[{"type":"text","text":"ok"}],"stop_reason":"end_turn",)"
                             R"("results":[]})";
    RequestLineRecorder proxyA(body);
    RequestLineRecorder proxyB(body);
    std::atomic<bool> running{true};

    Live<DockOptions> network{DockOptions{proxyA.url()}};
    OpenAIStandard openAI(network, &running);
    AnthropicStandard anthropic(network, &running);
    ChatModel model;
    model.endpoint = "http://model.invalid/v1/chat";
    model.api_key = "test-key";
    ChatRequest request;
    request.history.push_back({"user", "hi"});

    WebSearchOptions searchOptions;
    searchOptions.endpoint = "http://search.invalid/search";
    searchOptions.apiKey = "search-key";
    searchOptions.proxy = proxyA.url();
    Live<WebSearchOptions> liveSearch(searchOptions);
    TavilySearchProvider search(liveSearch, &running);
    SearchRequest query;
    query.query = "query";
    query.maxResults = 1;

    WebFetchOptions fetchOptions;
    fetchOptions.cacheTtlSeconds = 0; // 不缓存：两轮抓取都真的发请求
    fetchOptions.proxy = proxyA.url();
    Live<WebFetchOptions> liveFetch(fetchOptions);
    WebFetchAction fetch(liveFetch, &running);
    const nlohmann::json page = {{"url", "http://fetch.invalid/page"}};

    const auto sendAll = [&]() {
        EXPECT_EQ(openAI.request_chat(model, "model", request).code, 200);
        EXPECT_EQ(anthropic.request_chat(model, "model", request).code, 200);
        EXPECT_NO_THROW(search.search(query));
        const nlohmann::json fetched = nlohmann::json::parse(fetch.execute(page, {}).content);
        EXPECT_FALSE(fetched.contains("error")) << fetched.dump();
    };
    const std::vector<std::string> expected = {
        "POST http://model.invalid/v1/chat HTTP/1.1", "POST http://model.invalid/v1/chat HTTP/1.1",
        "POST http://search.invalid/search HTTP/1.1", "GET http://fetch.invalid/page HTTP/1.1"};

    sendAll();
    EXPECT_EQ(proxyA.lines(), expected);
    EXPECT_TRUE(proxyB.lines().empty());

    network.set(DockOptions{proxyB.url()});
    searchOptions.proxy = proxyB.url();
    liveSearch.set(searchOptions);
    fetchOptions.proxy = proxyB.url();
    liveFetch.set(fetchOptions);
    sendAll();
    EXPECT_EQ(proxyA.lines(), expected) << "换代理之后不再经过旧代理";
    EXPECT_EQ(proxyB.lines(), expected);
}
