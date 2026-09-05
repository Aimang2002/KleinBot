#include <gtest/gtest.h>

#include "Network/WebSocketApiChannel.h"
#include "Protocol/OneBot/OneBotEventDecoder.h"

#include <future>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
// call() 在异步线程里入队 action，轮询等待避免竞态
bool popActionWithRetry(WebSocketApiChannel &channel, OneBotAction &out)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (channel.tryPopAction(out))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
}

TEST(OneBotApiResultDecodeTest, DecodesOkFailedAndAsyncResponseFrames)
{
    OneBotEventDecoder decoder;

    const auto ok = decoder.decodeResponse(
        R"({"status":"ok","retcode":0,"data":{"app_name":"NapCat"},"echo":7})");
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok->echo, 7);
    EXPECT_EQ(ok->status, "ok");
    EXPECT_EQ(ok->retcode, 0);
    EXPECT_EQ(ok->data.value("app_name", ""), "NapCat");
    EXPECT_FALSE(ok->networkError);

    const auto failed = decoder.decodeResponse(
        R"({"status":"failed","retcode":1200,"data":null,"echo":9})");
    ASSERT_TRUE(failed.has_value());
    EXPECT_EQ(failed->status, "failed");
    EXPECT_EQ(failed->retcode, 1200);

    const auto async = decoder.decodeResponse(R"({"status":"async","retcode":1,"echo":11})");
    ASSERT_TRUE(async.has_value());
    EXPECT_EQ(async->status, "async");
    EXPECT_TRUE(async->data.is_null());
}

TEST(OneBotApiResultDecodeTest, IgnoresEventFramesAndUnCorrelatableFrames)
{
    OneBotEventDecoder decoder;

    // 事件帧：即使带 echo 也不当响应
    EXPECT_FALSE(decoder
                     .decodeResponse(
                         R"({"post_type":"message","message_type":"private","echo":3})")
                     .has_value());
    // 无 echo 的帧（历史 fire-and-forget 行为的响应）无法关联
    EXPECT_FALSE(decoder.decodeResponse(R"({"status":"ok","retcode":0})").has_value());
    // 非对象
    EXPECT_FALSE(decoder.decodeResponse("[1,2,3]").has_value());
}

TEST(OneBotActionEchoTest, EchoOnlySerializedWhenPositive)
{
    OneBotAction silent;
    silent.action = "get_version_info";
    EXPECT_EQ(silent.toJson(), nlohmann::json::parse(
                                   R"({"action":"get_version_info","params":{}})"));

    OneBotAction echoed;
    echoed.action = "get_version_info";
    echoed.echo = 42;
    EXPECT_EQ(echoed.toJson(), nlohmann::json::parse(
                                   R"({"action":"get_version_info","params":{},"echo":42})"));
}

TEST(WebSocketApiChannelTest, CallResolvesWhenTransportThreadDeliversResponse)
{
    WebSocketApiChannel channel;
    auto caller = std::async(std::launch::async, [&] {
        return channel.call("get_version_info", nlohmann::json::object(),
                            std::chrono::seconds(5));
    });

    OneBotAction action;
    ASSERT_TRUE(popActionWithRetry(channel, action));
    EXPECT_EQ(action.action, "get_version_info");
    EXPECT_GT(action.echo, 0);
    EXPECT_EQ(action.toJson()["echo"], action.echo);

    OneBotApiResult response;
    response.echo = action.echo;
    response.status = "ok";
    response.retcode = 0;
    response.data = nlohmann::json({{"app_name", "NapCat"}});
    channel.resolve(std::move(response));

    const OneBotApiResult result = caller.get();
    EXPECT_FALSE(result.networkError);
    EXPECT_EQ(result.echo, action.echo);
    EXPECT_EQ(result.data.value("app_name", ""), "NapCat");
}

TEST(WebSocketApiChannelTest, CallTimesOutWithNetworkErrorAndLateResolveIsDropped)
{
    WebSocketApiChannel channel;
    const OneBotApiResult result = channel.call("get_version_info", nlohmann::json::object(),
                                                std::chrono::milliseconds(50));
    EXPECT_TRUE(result.networkError);

    // 超时后响应帧才到：静默丢弃，不影响后续调用
    OneBotApiResult late;
    late.echo = result.echo;
    late.status = "ok";
    channel.resolve(std::move(late));

    // 通道仍可正常使用。超时调用的 action 仍留在队列（语义：下一条连接上补发，
    // 实现端未曾收到过不算重复），先弹出残留的再处理新调用
    auto retry = std::async(std::launch::async, [&] {
        return channel.call("get_version_info", nlohmann::json::object(),
                            std::chrono::seconds(5));
    });
    OneBotAction staleAction;
    OneBotAction action;
    ASSERT_TRUE(popActionWithRetry(channel, staleAction));
    EXPECT_EQ(staleAction.echo, result.echo);
    ASSERT_TRUE(popActionWithRetry(channel, action));
    EXPECT_NE(action.echo, result.echo);
    OneBotApiResult response;
    response.echo = action.echo;
    response.status = "ok";
    channel.resolve(std::move(response));
    EXPECT_FALSE(retry.get().networkError);
}

TEST(WebSocketApiChannelTest, OutOfOrderResponsesResolveCorrectCallers)
{
    WebSocketApiChannel channel;
    auto first = std::async(std::launch::async, [&] {
        return channel.call("a", nlohmann::json::object(), std::chrono::seconds(5));
    });
    auto second = std::async(std::launch::async, [&] {
        return channel.call("b", nlohmann::json::object(), std::chrono::seconds(5));
    });

    // 两个 async 的入队顺序不定，按实际弹出顺序记录，resolve 时用真实 echo 配对
    OneBotAction poppedFirst;
    OneBotAction poppedSecond;
    ASSERT_TRUE(popActionWithRetry(channel, poppedFirst));
    ASSERT_TRUE(popActionWithRetry(channel, poppedSecond));
    EXPECT_NE(poppedFirst.echo, poppedSecond.echo);

    // 故意按与弹出相反的顺序兑现
    OneBotApiResult responseForSecondCaller;
    responseForSecondCaller.echo = poppedFirst.echo;
    responseForSecondCaller.status = "ok";
    responseForSecondCaller.data = "first-popped";
    channel.resolve(std::move(responseForSecondCaller));
    OneBotApiResult responseForFirstCaller;
    responseForFirstCaller.echo = poppedSecond.echo;
    responseForFirstCaller.status = "ok";
    responseForFirstCaller.data = "second-popped";
    channel.resolve(std::move(responseForFirstCaller));

    // 每个调用者拿到的都是自己 echo 对应的数据（顺序与哪个线程先入队无关）
    const OneBotApiResult firstResult = first.get();
    const OneBotApiResult secondResult = second.get();
    EXPECT_FALSE(firstResult.networkError);
    EXPECT_FALSE(secondResult.networkError);
    const std::string firstGot = firstResult.data.get<std::string>();
    const std::string secondGot = secondResult.data.get<std::string>();
    EXPECT_TRUE((firstGot == "first-popped" && secondGot == "second-popped") ||
                (firstGot == "second-popped" && secondGot == "first-popped"));
    EXPECT_NE(firstGot, secondGot);
}

TEST(WebSocketApiChannelTest, FailAllReleasesPendingCallersOnSessionTeardown)
{
    WebSocketApiChannel channel;
    auto caller = std::async(std::launch::async, [&] {
        return channel.call("get_version_info", nlohmann::json::object(),
                            std::chrono::seconds(30));
    });

    // 等调用入队
    OneBotAction action;
    while (!channel.tryPopAction(action))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    channel.failAll("测试断开");
    const OneBotApiResult result = caller.get();
    EXPECT_TRUE(result.networkError);
    EXPECT_EQ(result.echo, action.echo);
}

TEST(WebSocketApiChannelTest, PendingCapRejectsNewCallersImmediately)
{
    WebSocketApiChannel channel(2);

    std::vector<std::future<OneBotApiResult>> callers;
    for (int i = 0; i < 2; ++i)
    {
        callers.push_back(std::async(std::launch::async, [&] {
            return channel.call("slow", nlohmann::json::object(), std::chrono::seconds(30));
        }));
    }
    OneBotAction action;
    int popped = 0;
    while (popped < 2)
    {
        if (channel.tryPopAction(action))
            ++popped;
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // 第三个调用立即被拒：不排队、不阻塞
    const OneBotApiResult rejected = channel.call("slow", nlohmann::json::object(),
                                                  std::chrono::seconds(30));
    EXPECT_TRUE(rejected.networkError);

    channel.failAll("测试收尾");
    for (auto &caller : callers)
        EXPECT_TRUE(caller.get().networkError);
}
