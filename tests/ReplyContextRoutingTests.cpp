#include <gtest/gtest.h>

#include "Application/ReplyContextRouting.h"

#include "Protocol/OneBot/OneBotEventDecoder.h"

namespace
{
InboundMessage groupMessageFrom(std::uint64_t sender, std::vector<std::uint64_t> mentioned)
{
    InboundMessage data;
    data.post_type = "message";
    data.message_type = "group";
    data.user_id = sender;
    data.group_id = 8823;
    data.message_id = 5566;
    data.mentioned_ids = std::move(mentioned);
    return data;
}
}

TEST(ReplyContextRoutingTest, MentionedBotGetsQuoteAndAt)
{
    const BotIdentity bot{10086, 10002, "Klein"};
    const InboundMessage data = groupMessageFrom(20001, {10086});

    const std::optional<ReplyContext> reply = buildReplyContext(data, bot);

    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(reply->at_user_id, "20001");
    EXPECT_EQ(reply->message_id, 5566);
}

TEST(ReplyContextRoutingTest, UnmentionedMessageGetsNoReplyContext)
{
    const BotIdentity bot{10086, 10002, "Klein"};

    // 没指向 bot：接话/观察路径的裸消息语义（v2.4.2 起有消费场景）
    const InboundMessage unmentioned = groupMessageFrom(20001, {30001});
    EXPECT_FALSE(buildReplyContext(unmentioned, bot).has_value());

    // @ 的是别人
    const InboundMessage others = groupMessageFrom(20001, {});
    EXPECT_FALSE(buildReplyContext(others, bot).has_value());

    // bot 自身消息的回声不指向自己
    const InboundMessage selfEcho = groupMessageFrom(10086, {10086});
    EXPECT_FALSE(buildReplyContext(selfEcho, bot).has_value());
}

TEST(OneBotEventDecoderMentionTest, ParsesAtSegmentsInBothIdForms)
{
    OneBotEventDecoder decoder;
    const auto event = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [
            {"type": "at", "data": {"qq": 10086}},
            {"type": "text", "data": {"text": " 在吗"}},
            {"type": "at", "data": {"qq": "30001"}}
        ],
        "message_id": 7788,
        "time": 1757010500
    })");

    ASSERT_TRUE(event.has_value());
    ASSERT_EQ(event->mentioned_ids.size(), 2u);
    EXPECT_EQ(event->mentioned_ids[0], 10086ULL);
    EXPECT_EQ(event->mentioned_ids[1], 30001ULL);
    EXPECT_EQ(event->plain_text, " 在吗");

    // "all"（@全体成员）是广播不是点名：不记录，群聊触发门槛不受其影响
    const auto broadcast = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [
            {"type": "at", "data": {"qq": "all"}},
            {"type": "text", "data": {"text": " 通知"}}
        ],
        "message_id": 7789,
        "time": 1757010501
    })");
    ASSERT_TRUE(broadcast.has_value());
    EXPECT_TRUE(broadcast->mentioned_ids.empty());
}

TEST(OneBotEventDecoderMentionTest, MentionedIdsReplaceLegacyStringMatching)
{
    OneBotEventDecoder decoder;

    // 旧行为缺陷回归：正文里的 bot QQ 号数字（无 at 段）不再构成触发
    const auto textOnly = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [{"type": "text", "data": {"text": "我叫10086你好"}}],
        "message_id": 7790,
        "time": 1757010502
    })");
    ASSERT_TRUE(textOnly.has_value());
    EXPECT_TRUE(textOnly->mentioned_ids.empty());
}

TEST(OneBotEventDecoderMentionTest, MessageIdStringAndMissingFormsAreGraceful)
{
    OneBotEventDecoder decoder;

    // 字符串形态（NapCat 新版）：不抛异常，进 raw 供 reply 段原样回填
    const auto stringId = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [{"type": "at", "data": {"qq": 10086}}],
        "message_id": "MsajdvLqdzYxMQA=",
        "time": 1757010503
    })");
    ASSERT_TRUE(stringId.has_value());
    EXPECT_EQ(stringId->message_id, 0);
    EXPECT_EQ(stringId->message_id_raw, "MsajdvLqdzYxMQA=");

    // 缺失形态：message_id 为 0，事件照常解析（引用降级为仅@）
    const auto missingId = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [{"type": "at", "data": {"qq": 10086}}],
        "time": 1757010504
    })");
    ASSERT_TRUE(missingId.has_value());
    EXPECT_EQ(missingId->message_id, 0);
    EXPECT_TRUE(missingId->message_id_raw.empty());

    // 畸形 at 段（缺 qq 字段）：跳过该段，事件其余部分照常解析
    const auto malformedAt = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [
            {"type": "at", "data": {}},
            {"type": "text", "data": {"text": "在吗"}}
        ],
        "message_id": 7791,
        "time": 1757010505
    })");
    ASSERT_TRUE(malformedAt.has_value());
    EXPECT_TRUE(malformedAt->mentioned_ids.empty());
    EXPECT_EQ(malformedAt->plain_text, "在吗");
}
