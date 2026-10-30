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

TEST(ReplyContextRoutingTest, FollowUpSegmentsSkipReplyContext)
{
    const BotIdentity bot{10086, 10002, "Klein"};
    const InboundMessage data = groupMessageFrom(20001, {10086});

    // 本轮第一条出站：引用原消息 + @ 发起人
    EXPECT_TRUE(buildReplyContext(data, bot, true).has_value());

    // 同一轮的后续分条 / 多消息：不重复指向同一条 @ 消息
    EXPECT_FALSE(buildReplyContext(data, bot, false).has_value());
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
    // at 段独立于 text 段：拼接后的 plain_text 统一去首尾空白，
    // 群聊里带 @ 的命令才不会因残留空格匹配不上
    EXPECT_EQ(event->plain_text, "在吗");

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

TEST(OneBotEventDecoderMentionTest, MentionedCommandTextIsCommandReady)
{
    OneBotEventDecoder decoder;

    // 群聊 @bot 发命令：raw_message 带 [CQ:at,...] 前缀不能用于匹配，
    // plain_text 去首尾空白后必须与纯文本命令逐字相等
    const auto command = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "raw_message": "[CQ:at,qq=10086] #帮助",
        "message": [
            {"type": "at", "data": {"qq": 10086}},
            {"type": "text", "data": {"text": " #帮助 "}}
        ],
        "message_id": 7792,
        "time": 1757010506
    })");
    ASSERT_TRUE(command.has_value());
    ASSERT_EQ(command->mentioned_ids.size(), 1u);
    EXPECT_EQ(command->plain_text, "#帮助");

    // 纯@无文字：plain_text 为空，由消息层注入情境占位，模型不会收到空 user 消息
    const auto atOnly = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [{"type": "at", "data": {"qq": 10086}}],
        "message_id": 7793,
        "time": 1757010507
    })");
    ASSERT_TRUE(atOnly.has_value());
    EXPECT_TRUE(atOnly->plain_text.empty());
}

// 引用回复段（T7b）：被引用消息 ID 双形态解析，供观察通道还原回复链
TEST(OneBotEventDecoderMentionTest, ParsesReplySegmentInBothIdForms)
{
    OneBotEventDecoder decoder;

    const auto numeric = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [
            {"type": "reply", "data": {"id": 7788}},
            {"type": "text", "data": {"text": "同感"}}
        ],
        "message_id": 7795,
        "time": 1757010509
    })");
    ASSERT_TRUE(numeric.has_value());
    EXPECT_EQ(numeric->reply_to_message_id, 7788);
    EXPECT_TRUE(numeric->reply_to_message_id_raw.empty());

    // 字符串形态（NapCat 新版）：原样保存
    const auto stringId = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [
            {"type": "reply", "data": {"id": "CAQABAKDKogQAIgQ0Njg="}},
            {"type": "text", "data": {"text": "是"}}
        ],
        "message_id": 7796,
        "time": 1757010510
    })");
    ASSERT_TRUE(stringId.has_value());
    EXPECT_EQ(stringId->reply_to_message_id, 0);
    EXPECT_EQ(stringId->reply_to_message_id_raw, "CAQABAKDKogQAIgQ0Njg=");

    // 无 reply 段：字段保持默认
    const auto plain = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [{"type": "text", "data": {"text": "新话题"}}],
        "message_id": 7797,
        "time": 1757010511
    })");
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(plain->reply_to_message_id, 0);
    EXPECT_TRUE(plain->reply_to_message_id_raw.empty());
}

// R1.1 零成本表情信号：image 段 sub_type/summary、mface、face 段
TEST(OneBotEventDecoderStickerTest, ParsesStickerImageMfaceAndFaceSegments)
{
    OneBotEventDecoder decoder;

    const auto sticker = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [
            {"type": "text", "data": {"text": "哈哈"}},
            {"type": "image", "data": {"url": "http://img/s.gif", "sub_type": 1, "summary": "[动画表情]"}},
            {"type": "mface", "data": {"emoji_id": "abc", "summary": "[狗头]"}},
            {"type": "face", "data": {"id": "14"}},
            {"type": "face", "data": {"id": 178}}
        ],
        "message_id": 7801,
        "time": 1757010600
    })");
    ASSERT_TRUE(sticker.has_value());
    EXPECT_EQ(sticker->plain_text, "哈哈");
    EXPECT_EQ(sticker->message_data_url, "http://img/s.gif");
    EXPECT_TRUE(sticker->image_is_sticker);
    EXPECT_EQ(sticker->image_summary, "[动画表情]");
    ASSERT_EQ(sticker->face_summaries.size(), 3U);
    EXPECT_EQ(sticker->face_summaries[0], "[狗头]");
    EXPECT_EQ(sticker->face_summaries[1], "face:14");
    EXPECT_EQ(sticker->face_summaries[2], "face:178");

    // LLOneBot 键名是驼峰 subType（2026-10-30 源码核对 + 线上实测）：非零即表情包形态
    //（7=贴图、13=热搜图）；此前只读 NapCat 形态的 sub_type，线上贴纸标记从未生效
    const auto camel = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [{"type": "image", "data": {"url": "http://img/u.jpg", "subType": 7, "file": "u.jpg"}}],
        "message_id": 7803,
        "time": 1757010602
    })");
    ASSERT_TRUE(camel.has_value());
    EXPECT_TRUE(camel->image_is_sticker);

    // 无正文段打标签：视频 / 语音 / 文件（带名）/ 分享卡片（带 prompt）/ 合并转发
    const auto labels = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [
            {"type": "video", "data": {"url": "http://v/a.mp4", "file": "a.mp4"}},
            {"type": "record", "data": {"url": "http://v/a.amr"}},
            {"type": "file", "data": {"file": "7B0A6697.png", "file_size": "38895864"}},
            {"type": "json", "data": {"data": "{\"ver\":\"1.0\",\"prompt\":\"[QQ小程序]某视频\"}"}},
            {"type": "json", "data": {"data": "not json"}},
            {"type": "forward", "data": {"id": "123"}}
        ],
        "message_id": 7804,
        "time": 1757010603
    })");
    ASSERT_TRUE(labels.has_value());
    EXPECT_TRUE(labels->plain_text.empty());
    EXPECT_TRUE(labels->message_data_url.empty()) << "视频/语音的 url 不是图片，不进 message_data_url";
    ASSERT_EQ(labels->attachment_labels.size(), 6U);
    EXPECT_EQ(labels->attachment_labels[0], "[视频]");
    EXPECT_EQ(labels->attachment_labels[1], "[语音]");
    EXPECT_EQ(labels->attachment_labels[2], "[文件 7B0A6697.png]");
    EXPECT_EQ(labels->attachment_labels[3], "[卡片 [QQ小程序]某视频]");
    EXPECT_EQ(labels->attachment_labels[4], "[卡片]");
    EXPECT_EQ(labels->attachment_labels[5], "[合并转发]");

    // sub_type 字符串形态（JS 系实现端）也算；无 summary 时仍按 sub_type 判定
    const auto stringSubType = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [{"type": "image", "data": {"url": "http://img/t.gif", "sub_type": "1"}}],
        "message_id": 7802,
        "time": 1757010601
    })");
    ASSERT_TRUE(stringSubType.has_value());
    EXPECT_TRUE(stringSubType->image_is_sticker);
    EXPECT_TRUE(stringSubType->image_summary.empty());

    // 普通图片：sub_type=0、无 summary → 不是贴纸
    const auto photo = decoder.decode(R"({
        "post_type": "message",
        "message_type": "group",
        "user_id": 20001,
        "group_id": 8823,
        "message": [{"type": "image", "data": {"url": "http://img/p.jpg", "sub_type": 0}}],
        "message_id": 7803,
        "time": 1757010602
    })");
    ASSERT_TRUE(photo.has_value());
    EXPECT_FALSE(photo->image_is_sticker);
    EXPECT_TRUE(photo->face_summaries.empty());

    // 私聊纯文本：新字段保持默认，原有字段不变
    const auto privateText = decoder.decode(R"({
        "post_type": "message",
        "message_type": "private",
        "user_id": 30001,
        "message": [{"type": "text", "data": {"text": "你好"}}],
        "message_id": 7804,
        "time": 1757010603
    })");
    ASSERT_TRUE(privateText.has_value());
    EXPECT_EQ(privateText->plain_text, "你好");
    EXPECT_TRUE(privateText->message_data_url.empty());
    EXPECT_FALSE(privateText->image_is_sticker);
    EXPECT_TRUE(privateText->image_summary.empty());
    EXPECT_TRUE(privateText->face_summaries.empty());
    EXPECT_EQ(privateText->message_id, 7804);
}
