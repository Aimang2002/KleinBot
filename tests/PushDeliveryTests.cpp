#include <gtest/gtest.h>

#include "Port/MessageSenderPort.h"
#include "Port/OutboundDelivery.h"
#include "Port/OutboundMessage.h"
#include "Push/PushContent.h"
#include "Push/PushDelivery.h"

#include <string>
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

PushSettings makeSettings()
{
    PushSettings settings;
    settings.enabled = true;
    settings.accessToken = "token";
    settings.allowedGroups = {123456};
    settings.allowedUsers = {654321};
    settings.maxPerHour = 60;
    return settings;
}

PushContent text(const std::string &data)
{
    return {"text", data, ""};
}

PushContent image(const std::string &data)
{
    return {"image", data, "image/png"};
}

PushTarget group()
{
    return {true, 123456};
}

PushTarget stranger()
{
    return {true, 999999};
}
}

TEST(PushDeliveryTest, RendersTextAndDeliversToWhitelistedGroup)
{
    RecordingSender sender;
    PushDelivery delivery(makeSettings(), sender,
                          [](const std::string &) { return "润色后的口吻文本"; });

    EXPECT_EQ(delivery.deliver(group(), {text("原始内容")}, false), PushReject::Ok);
    ASSERT_EQ(sender.deliveries.size(), 1U);
    const OutboundDelivery &out = sender.deliveries.front();
    ASSERT_TRUE(std::holds_alternative<GroupMessageTarget>(out.target));
    EXPECT_EQ(std::get<GroupMessageTarget>(out.target).group_id, "123456");
    ASSERT_TRUE(std::holds_alternative<TextMessage>(out.message));
    EXPECT_EQ(std::get<TextMessage>(out.message).content, "润色后的口吻文本");
}

TEST(PushDeliveryTest, FallsBackToRawTextWhenRendererFails)
{
    RecordingSender sender;
    PushDelivery delivery(makeSettings(), sender, [](const std::string &) { return ""; });

    EXPECT_EQ(delivery.deliver(group(), {text("原始内容")}, false), PushReject::Ok);
    ASSERT_EQ(sender.deliveries.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<TextMessage>(sender.deliveries.front().message));
    EXPECT_EQ(std::get<TextMessage>(sender.deliveries.front().message).content, "原始内容");
}

TEST(PushDeliveryTest, SkipRenderSendsRawTextDirectly)
{
    RecordingSender sender;
    bool rendererCalled = false;
    PushDelivery delivery(makeSettings(), sender,
                          [&rendererCalled](const std::string &) {
                              rendererCalled = true;
                              return "不应被使用";
                          });

    EXPECT_EQ(delivery.deliver(group(), {text("原文")}, true), PushReject::Ok);
    EXPECT_FALSE(rendererCalled);
    ASSERT_EQ(sender.deliveries.size(), 1U);
    EXPECT_EQ(std::get<TextMessage>(sender.deliveries.front().message).content, "原文");
}

TEST(PushDeliveryTest, DeliversImageAsBase64Message)
{
    RecordingSender sender;
    PushDelivery delivery(makeSettings(), sender, [](const std::string &) { return ""; });

    EXPECT_EQ(delivery.deliver(group(), {image("aGVsbG8=")}, false), PushReject::Ok);
    ASSERT_EQ(sender.deliveries.size(), 1U);
    const OutboundMessage &message = sender.deliveries.front().message;
    ASSERT_TRUE(std::holds_alternative<ImageMessage>(message));
    EXPECT_EQ(std::get<ImageMessage>(message).source, ImageMessage::Source::Base64);
    EXPECT_EQ(std::get<ImageMessage>(message).data, "aGVsbG8=");
}

TEST(PushDeliveryTest, DeliversMultipleBlocksInOrder)
{
    RecordingSender sender;
    PushDelivery delivery(makeSettings(), sender, [](const std::string &) { return ""; });

    EXPECT_EQ(delivery.deliver(group(), {text("第一段"), image("aW1n"), text("第二段")}, true),
              PushReject::Ok);
    ASSERT_EQ(sender.deliveries.size(), 3U);
    EXPECT_TRUE(std::holds_alternative<TextMessage>(sender.deliveries[0].message));
    EXPECT_TRUE(std::holds_alternative<ImageMessage>(sender.deliveries[1].message));
    EXPECT_TRUE(std::holds_alternative<TextMessage>(sender.deliveries[2].message));
}

TEST(PushDeliveryTest, RejectsTargetOutsideWhitelistWithoutPartialDelivery)
{
    RecordingSender sender;
    PushDelivery delivery(makeSettings(), sender, [](const std::string &) { return ""; });

    EXPECT_EQ(delivery.deliver(stranger(), {text("内容")}, false), PushReject::DeniedTarget);
    EXPECT_TRUE(sender.deliveries.empty());

    // 用户白名单存在但该用户未列入时同样拒绝
    PushTarget unlistedUser{false, 111111};
    EXPECT_EQ(delivery.deliver(unlistedUser, {text("内容")}, false), PushReject::DeniedTarget);
    EXPECT_TRUE(sender.deliveries.empty());
}

TEST(PushDeliveryTest, WhitelistedPrivateTargetIsAllowed)
{
    RecordingSender sender;
    PushDelivery delivery(makeSettings(), sender, [](const std::string &) { return ""; });

    PushTarget user{false, 654321};
    EXPECT_EQ(delivery.deliver(user, {text("私聊内容")}, true), PushReject::Ok);
    ASSERT_EQ(sender.deliveries.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<DirectMessageTarget>(sender.deliveries.front().target));
    EXPECT_EQ(std::get<DirectMessageTarget>(sender.deliveries.front().target).user_id, "654321");
}

TEST(PushDeliveryTest, EnforcesPerTargetRateLimit)
{
    PushSettings settings = makeSettings();
    settings.maxPerHour = 2;
    RecordingSender sender;
    PushDelivery delivery(settings, sender, [](const std::string &) { return ""; });

    EXPECT_EQ(delivery.deliver(group(), {text("一")}, true), PushReject::Ok);
    EXPECT_EQ(delivery.deliver(group(), {text("二")}, true), PushReject::Ok);
    EXPECT_EQ(delivery.deliver(group(), {text("三")}, true), PushReject::RateLimited);
    EXPECT_EQ(sender.deliveries.size(), 2U);

    // 频率帽按目标独立计数：白名单私聊目标不受群目标触顶影响
    PushTarget user{false, 654321};
    EXPECT_EQ(delivery.deliver(user, {text("私聊")}, true), PushReject::Ok);
}

TEST(PushDeliveryTest, RejectsOversizedTextAndImage)
{
    RecordingSender sender;
    PushDelivery delivery(makeSettings(), sender, [](const std::string &) { return ""; });

    EXPECT_EQ(delivery.deliver(group(), {text(std::string(5000, 'x'))}, true),
              PushReject::TextTooLarge);
    EXPECT_EQ(delivery.deliver(group(), {image(std::string(6ULL * 1024ULL * 1024ULL + 1, 'x'))}, true),
              PushReject::ImageTooLarge);
    EXPECT_TRUE(sender.deliveries.empty());
}

TEST(PushDeliveryTest, RejectsUnsupportedContentTypeWithSupportedListSemantics)
{
    RecordingSender sender;
    PushDelivery delivery(makeSettings(), sender, [](const std::string &) { return ""; });

    EXPECT_EQ(delivery.deliver(group(), {{"video", "data", "video/mp4"}}, true),
              PushReject::UnsupportedType);
    EXPECT_TRUE(sender.deliveries.empty());
}

TEST(PushDeliveryTest, RejectsEmptyContentsAndEmptyDataWithoutPartialDelivery)
{
    RecordingSender sender;
    PushDelivery delivery(makeSettings(), sender, [](const std::string &) { return ""; });

    EXPECT_EQ(delivery.deliver(group(), {}, true), PushReject::InvalidContent);
    EXPECT_EQ(delivery.deliver(group(), {text("")}, true), PushReject::InvalidContent);
    // 第二块非法时第一块也不投递（整次原子拒绝）
    EXPECT_EQ(delivery.deliver(group(), {text("好"), text("")}, true), PushReject::InvalidContent);
    EXPECT_TRUE(sender.deliveries.empty());
}
