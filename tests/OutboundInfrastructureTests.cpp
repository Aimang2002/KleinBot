#include <gtest/gtest.h>

#include "MessageQueue/OutboundMessageQueue.h"
#include "MessageSender/QueuedMessageSender.h"
#include "Protocol/OneBot/OneBotMessageEncoder.h"

TEST(OutboundMessageQueueTest, PreservesSemanticDeliveryOrder)
{
    OutboundMessageQueue queue;
    queue.push(OutboundDelivery{DirectMessageTarget{"42"}, TextMessage{"first"}});
    queue.push(OutboundDelivery{GroupMessageTarget{"88"}, TextMessage{"second"}});

    auto first = queue.tryPop();
    auto second = queue.tryPop();

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(std::get<DirectMessageTarget>(first->target).user_id, "42");
    EXPECT_EQ(std::get<TextMessage>(first->message).content, "first");
    EXPECT_EQ(std::get<GroupMessageTarget>(second->target).group_id, "88");
    EXPECT_EQ(std::get<TextMessage>(second->message).content, "second");
    EXPECT_FALSE(queue.tryPop().has_value());
}

TEST(QueuedMessageSenderTest, EnqueuesProtocolIndependentTargets)
{
    OutboundMessageQueue queue;
    QueuedMessageSender sender(queue);

    sender.send_private(42, TextMessage{"private"});
    sender.send_group(88, ImageMessage{ImageMessage::Source::Url, "https://example.test/image.png"});

    auto privateDelivery = queue.tryPop();
    auto groupDelivery = queue.tryPop();

    ASSERT_TRUE(privateDelivery.has_value());
    ASSERT_TRUE(groupDelivery.has_value());
    EXPECT_EQ(std::get<DirectMessageTarget>(privateDelivery->target).user_id, "42");
    EXPECT_EQ(std::get<TextMessage>(privateDelivery->message).content, "private");
    EXPECT_EQ(std::get<GroupMessageTarget>(groupDelivery->target).group_id, "88");
    EXPECT_EQ(std::get<ImageMessage>(groupDelivery->message).data,
              "https://example.test/image.png");
}

TEST(OneBotMessageEncoderTest, PreservesPrivateTextEnvelopeContract)
{
    OneBotMessageEncoder encoder("send_private_msg", "send_group_msg");
    const OutboundDelivery delivery{DirectMessageTarget{"42"}, TextMessage{"hello"}};

    const nlohmann::json encoded = encoder.encode(delivery).toJson();

    EXPECT_EQ(encoded, nlohmann::json::parse(R"({
        "action": "send_private_msg",
        "params": {
            "user_id": 42,
            "message": [
                {"type": "text", "data": {"text": "hello"}}
            ]
        }
    })"));
}

TEST(OneBotMessageEncoderTest, EncodesAllExistingMessageVariants)
{
    OneBotMessageEncoder encoder("private", "group");

    const auto image = encoder.encode(OutboundDelivery{
        GroupMessageTarget{"88"},
        ImageMessage{ImageMessage::Source::LocalPath, "source/image.png"}
    });
    const auto music = encoder.encode(OutboundDelivery{
        DirectMessageTarget{"42"}, MusicMessage{123456}
    });
    const auto voice = encoder.encode(OutboundDelivery{
        DirectMessageTarget{"42"}, VoiceMessage{"source/voice.wav"}
    });

    EXPECT_EQ(image.action, "group");
    EXPECT_EQ(image.params.at("group_id"), 88);
    EXPECT_EQ(image.params.at("message").at(0).at("data").at("file"),
              "file:///source/image.png");
    EXPECT_EQ(music.params.at("message").at(0).at("data").at("type"), "163");
    EXPECT_EQ(music.params.at("message").at(0).at("data").at("id"), "123456");
    EXPECT_EQ(voice.params.at("message").at(0).at("type"), "record");
    EXPECT_EQ(voice.params.at("message").at(0).at("data").at("file"),
              "file:///source/voice.wav");
}

TEST(OneBotMessageEncoderTest, RejectsTargetsUnsupportedByOneBot)
{
    OneBotMessageEncoder encoder("private", "group");
    const OutboundDelivery delivery{DirectMessageTarget{"satori-user"}, TextMessage{"hello"}};

    EXPECT_THROW(encoder.encode(delivery), std::invalid_argument);
}
