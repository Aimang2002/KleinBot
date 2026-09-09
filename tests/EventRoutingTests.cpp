#include <gtest/gtest.h>

#include "Application/EventRouter.h"
#include "Event/FriendRequestNotifier.h"
#include "Network/OneBotApiChannel.h"
#include "Port/MessageSenderPort.h"
#include "Port/OutboundMessage.h"
#include "Protocol/OneBot/OneBotEventDecoder.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace
{
class RecordingHandler final : public EventHandler
{
public:
    void handle(const InboundMessage &event) override
    {
        received.push_back(event);
    }

    std::vector<InboundMessage> received;
};
}

TEST(OneBotEventDecoderNoticeTest, DecodesPokeNoticeWithNotifySubType)
{
    OneBotEventDecoder decoder;
    const auto event = decoder.decode(R"({
        "post_type": "notice",
        "notice_type": "notify",
        "sub_type": "poke",
        "user_id": 10001,
        "group_id": 8823,
        "target_id": 10086,
        "time": 1757010000
    })");

    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->post_type, "notice");
    EXPECT_EQ(event->notice_type, "notify");
    EXPECT_EQ(event->sub_type, "poke");
    EXPECT_EQ(event->user_id, 10001ULL);
    EXPECT_EQ(event->group_id, 8823ULL);
    EXPECT_EQ(event->target_id, 10086ULL);
    EXPECT_EQ(event->message_timestamp, 1757010000LL);
    EXPECT_TRUE(event->raw_message.empty());
}

TEST(OneBotEventDecoderNoticeTest, DecodesGroupIncreaseDecreaseNotices)
{
    OneBotEventDecoder decoder;
    const auto increase = decoder.decode(R"({
        "post_type": "notice",
        "notice_type": "group_increase",
        "sub_type": "approve",
        "user_id": 20001,
        "group_id": 8823,
        "operator_id": 10002,
        "time": 1757010100
    })");
    ASSERT_TRUE(increase.has_value());
    EXPECT_EQ(increase->notice_type, "group_increase");
    EXPECT_EQ(increase->sub_type, "approve");
    EXPECT_EQ(increase->operator_id, 10002ULL);

    const auto decrease = decoder.decode(R"({
        "post_type": "notice",
        "notice_type": "group_decrease",
        "sub_type": "leave",
        "user_id": 20002,
        "group_id": 8823,
        "operator_id": 20002,
        "time": 1757010200
    })");
    ASSERT_TRUE(decrease.has_value());
    EXPECT_EQ(decrease->notice_type, "group_decrease");
    EXPECT_EQ(decrease->sub_type, "leave");
}

TEST(OneBotEventDecoderNoticeTest, DecodesFriendRequestEvent)
{
    OneBotEventDecoder decoder;
    const auto request = decoder.decode(R"({
        "post_type": "request",
        "request_type": "friend",
        "user_id": 30001,
        "comment": "我是群里的XX，加个好友",
        "flag": "REQUEST_FLAG_123",
        "time": 1757010300
    })");

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->post_type, "request");
    EXPECT_EQ(request->request_type, "friend");
    EXPECT_EQ(request->user_id, 30001ULL);
    EXPECT_EQ(request->comment, "我是群里的XX，加个好友");
    EXPECT_EQ(request->flag, "REQUEST_FLAG_123");
    EXPECT_EQ(request->message_timestamp, 1757010300LL);
}

TEST(OneBotEventDecoderNoticeTest, MessageEventUnaffectedByNoticeFields)
{
    OneBotEventDecoder decoder;
    const auto message = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 10001,
        "group_id": 8823,
        "raw_message": "hello",
        "message": [{"type": "text", "data": {"text": "hello"}}],
        "message_id": 5566,
        "time": 1757010400,
        "sender": {"nickname": "Tester", "card": "卡片"}
    })");

    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->plain_text, "hello");
    EXPECT_TRUE(message->notice_type.empty());
    EXPECT_TRUE(message->request_type.empty());
    EXPECT_TRUE(message->flag.empty());
    EXPECT_EQ(message->target_id, 0ULL);
}

TEST(EventRouterTest, RouteKeyRules)
{
    InboundMessage poke;
    poke.post_type = "notice";
    poke.notice_type = "notify";
    poke.sub_type = "poke";
    EXPECT_EQ(EventRouter::routeKey(poke), "notice.notify.poke");

    // notify 家族之外不再细分
    InboundMessage increase;
    increase.post_type = "notice";
    increase.notice_type = "group_increase";
    increase.sub_type = "approve";
    EXPECT_EQ(EventRouter::routeKey(increase), "notice.group_increase");

    InboundMessage recall;
    recall.post_type = "notice";
    recall.notice_type = "group_recall";
    EXPECT_EQ(EventRouter::routeKey(recall), "notice.group_recall");

    InboundMessage friendRequest;
    friendRequest.post_type = "request";
    friendRequest.request_type = "friend";
    EXPECT_EQ(EventRouter::routeKey(friendRequest), "request.friend");
}

TEST(EventRouterTest, DispatchDeliversToSubscribersInOrder)
{
    EventRouter router;
    RecordingHandler first;
    RecordingHandler second;
    RecordingHandler other;

    router.subscribe("notice.notify.poke", first);
    router.subscribe("notice.notify.poke", second);
    router.subscribe("notice.group_increase", other);

    InboundMessage poke;
    poke.post_type = "notice";
    poke.notice_type = "notify";
    poke.sub_type = "poke";
    poke.user_id = 42;
    router.dispatch(poke);

    ASSERT_EQ(first.received.size(), 1u);
    EXPECT_EQ(first.received[0].user_id, 42ULL);
    ASSERT_EQ(second.received.size(), 1u);
    EXPECT_TRUE(other.received.empty());
}

TEST(EventRouterTest, UnsubscribedEventIsSilentlyDropped)
{
    EventRouter router;
    RecordingHandler handler;
    router.subscribe("notice.notify.poke", handler);

    InboundMessage essence;
    essence.post_type = "notice";
    essence.notice_type = "notify";
    essence.sub_type = "essence";
    router.dispatch(essence); // 无订阅者：不抛异常、不送达

    InboundMessage unknown;
    unknown.post_type = "notice";
    unknown.notice_type = "notify";
    unknown.sub_type = "lucky_king";
    router.dispatch(unknown);

    EXPECT_TRUE(handler.received.empty());
}

// ===== T6 事件回应 handler =====

namespace
{
class RecordingSender final : public MessageSenderPort
{
public:
    void deliver(OutboundDelivery delivery) override
    {
        delivered.push_back(std::move(delivery));
    }

    std::vector<OutboundDelivery> delivered;

    const std::string *textAt(std::size_t index) const
    {
        const auto *text = std::get_if<TextMessage>(&delivered[index].message);
        return text ? &text->content : nullptr;
    }
};

class FakeApiChannel final : public OneBotApiChannel
{
public:
    OneBotApiResult call(const std::string &action, nlohmann::json params,
                         std::chrono::milliseconds) override
    {
        actions.push_back(action);
        paramsList.push_back(std::move(params));
        return result;
    }

    std::vector<std::string> actions;
    std::vector<nlohmann::json> paramsList;
    OneBotApiResult result;
};

}

namespace
{
struct RequestHarness
{
    std::int64_t now = 1000;
    RecordingSender sender;
    FakeApiChannel api;
};

InboundMessage friendRequestEvent(const std::string &flag)
{
    InboundMessage event;
    event.post_type = "request";
    event.request_type = "friend";
    event.user_id = 30001;
    event.comment = "我是群里的XX";
    event.flag = flag;
    return event;
}
}

TEST(FriendRequestNotifierTest, NotifiesManagerOncePerFlag)
{
    RequestHarness harness;
    FriendRequestNotifier notifier(harness.sender, harness.api, 99999,
                                   [&] { return harness.now; });

    notifier.handle(friendRequestEvent("FLAG_A"));
    notifier.handle(friendRequestEvent("FLAG_A")); // 同 flag 去重
    notifier.handle(friendRequestEvent("FLAG_B")); // 不同 flag 正常通报

    ASSERT_EQ(harness.sender.delivered.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<DirectMessageTarget>(harness.sender.delivered[0].target));
    EXPECT_EQ(std::get<DirectMessageTarget>(harness.sender.delivered[0].target).user_id,
              "99999");
    const std::string &text = *harness.sender.textAt(0);
    EXPECT_NE(text.find("30001"), std::string::npos);
    EXPECT_NE(text.find("我是群里的XX"), std::string::npos);
    EXPECT_NE(text.find("FLAG_A"), std::string::npos);
    EXPECT_NE(text.find("后续版本"), std::string::npos) << "注明自动处理未开放";
}

TEST(FriendRequestNotifierTest, NicknameAppendedOnlyWhenStrangerInfoSucceeds)
{
    RequestHarness harness;
    harness.api.result.retcode = 0;
    harness.api.result.data = nlohmann::json({{"nickname", "小白"}});
    FriendRequestNotifier notifier(harness.sender, harness.api, 99999,
                                   [&] { return harness.now; });

    notifier.handle(friendRequestEvent("FLAG_A"));
    EXPECT_EQ(harness.api.actions.size(), 1u);
    EXPECT_EQ(harness.api.actions[0], "get_stranger_info");
    EXPECT_NE(harness.sender.textAt(0)->find("小白"), std::string::npos);

    harness.now += 10;
    harness.api.result.networkError = true; // 查询失败：省略昵称但照常通报
    notifier.handle(friendRequestEvent("FLAG_B"));
    const std::string &text = *harness.sender.textAt(1);
    EXPECT_EQ(text.find("小白"), std::string::npos);
    EXPECT_NE(text.find("30001"), std::string::npos);
}

TEST(FriendRequestNotifierTest, WithoutManagerConfiguredStaysSilent)
{
    RequestHarness harness;
    FriendRequestNotifier notifier(harness.sender, harness.api, 0,
                                   [&] { return harness.now; });

    notifier.handle(friendRequestEvent("FLAG_A"));

    EXPECT_TRUE(harness.sender.delivered.empty());
    EXPECT_TRUE(harness.api.actions.empty());
}
