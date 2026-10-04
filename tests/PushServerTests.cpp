#include <gtest/gtest.h>

#include "Port/MessageSenderPort.h"
#include "Port/OutboundDelivery.h"
#include "Port/OutboundMessage.h"
#include "Push/PushContent.h"
#include "Push/PushDelivery.h"
#include "Push/PushServer.h"

#include "../../Library/httplib/httplib.h"

#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
class RecordingSender : public MessageSenderPort
{
public:
    void deliver(OutboundDelivery delivery) override
    {
        deliveries.push_back(std::move(delivery));
    }

    std::vector<OutboundDelivery> deliveries;
};

// 本库版本的 Result 是指针式接口（result->status），统一经辅助函数取状态码
int postStatus(httplib::Client &client, const httplib::Headers &headers,
               const std::string &path, const std::string &body)
{
    const auto result = client.Post(path, headers, body, "application/json");
    EXPECT_TRUE(result != nullptr);
    return result ? result->status : 0;
}

class PushDoorFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        settings.enabled = true;
        settings.accessToken = "push-token";
        settings.allowedGroups = {123456};
        settings.allowedUsers = {654321};
        delivery = std::make_unique<PushDelivery>(
            settings, sender, [](const std::string &) { return "润色文本"; });
        server = PushServer::buildServer(settings, *delivery);
        port = server->bind_to_any_port("127.0.0.1");
        ASSERT_GT(port, 0);
        serverThread = std::make_unique<std::thread>([this]() { server->listen_after_bind(); });
    }

    void TearDown() override
    {
        server->stop();
        serverThread->join();
    }

    httplib::Headers authHeaders(const std::string &token = "push-token") const
    {
        return {{"Authorization", "Bearer " + token}};
    }

    int post(const httplib::Headers &headers, const std::string &body) const
    {
        httplib::Client client("127.0.0.1", port);
        return postStatus(client, headers, "/push", body);
    }

    PushSettings settings;
    RecordingSender sender;
    std::unique_ptr<PushDelivery> delivery;
    std::unique_ptr<httplib::Server> server;
    std::unique_ptr<std::thread> serverThread;
    int port = 0;
};

const char *validTextBody = R"({
    "target": {"type": "group", "id": 123456},
    "contents": [{"type": "text", "data": "第五期深渊开启了"}]
})";
}

TEST_F(PushDoorFixture, RejectsMissingOrWrongToken)
{
    EXPECT_EQ(post(httplib::Headers{}, validTextBody), 401);
    EXPECT_EQ(post(authHeaders("wrong-token"), validTextBody), 401);
    EXPECT_TRUE(sender.deliveries.empty());
}

TEST_F(PushDoorFixture, UnknownPathReturns404)
{
    {
        httplib::Client client("127.0.0.1", port);
        const auto result = client.Get("/", authHeaders());
        ASSERT_TRUE(result != nullptr);
        EXPECT_EQ(result->status, 404);
    }
    {
        // /push 之外无任何路由
        httplib::Client client("127.0.0.1", port);
        const auto result = client.Post("/other", authHeaders(), "{}", "application/json");
        ASSERT_TRUE(result != nullptr);
        EXPECT_EQ(result->status, 404);
    }
}

TEST_F(PushDoorFixture, AcceptsValidTextPushAndDelivers)
{
    httplib::Client client("127.0.0.1", port);
    const auto result = client.Post("/push", authHeaders(), validTextBody, "application/json");
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(result->status, 200);
    EXPECT_NE(result->body.find("accepted"), std::string::npos);
    ASSERT_EQ(sender.deliveries.size(), 1U);
    const OutboundDelivery &out = sender.deliveries.front();
    ASSERT_TRUE(std::holds_alternative<GroupMessageTarget>(out.target));
    EXPECT_EQ(std::get<GroupMessageTarget>(out.target).group_id, "123456");
    ASSERT_TRUE(std::holds_alternative<TextMessage>(out.message));
    // 默认走润色路径
    EXPECT_EQ(std::get<TextMessage>(out.message).content, "润色文本");
}

TEST_F(PushDoorFixture, AcceptsImageAndTextBlocksInOrder)
{
    const std::string body = R"({
        "target": {"type": "private", "id": 654321},
        "contents": [
            {"type": "text", "data": "配文"},
            {"type": "image", "data_base64": "aVbobG8=", "mime": "image/png"}
        ],
        "skip_render": true
    })";
    EXPECT_EQ(post(authHeaders(), body), 200);
    ASSERT_EQ(sender.deliveries.size(), 2U);
    ASSERT_TRUE(std::holds_alternative<DirectMessageTarget>(sender.deliveries[0].target));
    EXPECT_EQ(std::get<TextMessage>(sender.deliveries[0].message).content, "配文");
    ASSERT_TRUE(std::holds_alternative<ImageMessage>(sender.deliveries[1].message));
    EXPECT_EQ(std::get<ImageMessage>(sender.deliveries[1].message).data, "aVbobG8=");
}

TEST_F(PushDoorFixture, RejectsInvalidJsonWith400)
{
    EXPECT_EQ(post(authHeaders(), "{"), 400);
    EXPECT_EQ(post(authHeaders(), "[1,2,3]"), 400);
}

TEST_F(PushDoorFixture, RejectsMalformedRequestWith422)
{
    // 缺 target
    EXPECT_EQ(post(authHeaders(), R"({"contents": [{"type":"text","data":"x"}]})"), 422);
    // contents 空数组
    EXPECT_EQ(post(authHeaders(),
                   R"({"target": {"type": "group", "id": 123456}, "contents": []})"),
              422);
}

TEST_F(PushDoorFixture, RejectsUnsupportedContentTypeWithSupportedList)
{
    const std::string body = R"({
        "target": {"type": "group", "id": 123456},
        "contents": [{"type": "video", "data": "..."}]
    })";
    httplib::Client client("127.0.0.1", port);
    const auto result = client.Post("/push", authHeaders(), body, "application/json");
    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(result->status, 422);
    EXPECT_NE(result->body.find("unsupported_content_type"), std::string::npos);
    EXPECT_NE(result->body.find("text"), std::string::npos);
}

TEST_F(PushDoorFixture, RejectsTargetOutsideWhitelistWith403)
{
    const std::string body = R"({
        "target": {"type": "group", "id": 999999},
        "contents": [{"type": "text", "data": "内容"}]
    })";
    EXPECT_EQ(post(authHeaders(), body), 403);
    EXPECT_TRUE(sender.deliveries.empty());
}

TEST(PushServerRateTest, EnforcesRateLimitAtHttpLayer)
{
    PushSettings settings;
    settings.enabled = true;
    settings.accessToken = "push-token";
    settings.allowedGroups = {123456};
    settings.maxPerHour = 1;
    RecordingSender sender;
    PushDelivery delivery(settings, sender, [](const std::string &) { return ""; });
    auto server = PushServer::buildServer(settings, delivery);
    const int port = server->bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    std::thread serverThread([&server]() { server->listen_after_bind(); });
    httplib::Client client("127.0.0.1", port);
    const httplib::Headers headers = {{"Authorization", "Bearer push-token"}};

    EXPECT_EQ(postStatus(client, headers, "/push", validTextBody), 200);
    EXPECT_EQ(postStatus(client, headers, "/push", validTextBody), 429);
    EXPECT_EQ(sender.deliveries.size(), 1U);

    server->stop();
    serverThread.join();
}
