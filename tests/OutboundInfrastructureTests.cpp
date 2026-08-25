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
    OneBotMessageEncoder encoder;
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
    OneBotMessageEncoder encoder;

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

    EXPECT_EQ(image.action, "send_group_msg");
    EXPECT_EQ(image.params.at("group_id"), 88);
    EXPECT_EQ(image.params.at("message").at(0).at("data").at("file"),
              "file:///source/image.png");
    EXPECT_EQ(music.params.at("message").at(0).at("data").at("type"), "163");
    EXPECT_EQ(music.params.at("message").at(0).at("data").at("id"), "123456");
    EXPECT_EQ(voice.params.at("message").at(0).at("type"), "record");
    EXPECT_EQ(voice.params.at("message").at(0).at("data").at("file"),
              "file:///source/voice.wav");
}

// 本地文件统一经 localFileUrl 编码：POSIX 绝对路径 file://，Windows 盘符路径先归一为
// 正斜杠再拼 file:///，相对路径同样 file:///；图片 LocalPath 与语音 record 共用该规则
TEST(OneBotMessageEncoderTest, EncodesLocalFileUrlsForAbsoluteAndWindowsPaths)
{
    OneBotMessageEncoder encoder;

    const auto posixAbsolute = encoder.encode(OutboundDelivery{
        DirectMessageTarget{"42"}, VoiceMessage{"/tmp/kleinbot/1730000000_0.wav"}
    });
    const auto windowsAbsolute = encoder.encode(OutboundDelivery{
        DirectMessageTarget{"42"}, VoiceMessage{R"(C:\Users\klein\AppData\Local\Temp\kleinbot\a.wav)"}
    });
    const auto relative = encoder.encode(OutboundDelivery{
        DirectMessageTarget{"42"}, VoiceMessage{"source/voice.wav"}
    });
    const auto windowsImage = encoder.encode(OutboundDelivery{
        GroupMessageTarget{"88"}, ImageMessage{ImageMessage::Source::LocalPath, R"(source\image_assets\img_1.jpg)"}
    });

    EXPECT_EQ(posixAbsolute.params.at("message").at(0).at("data").at("file"),
              "file:///tmp/kleinbot/1730000000_0.wav");
    EXPECT_EQ(windowsAbsolute.params.at("message").at(0).at("data").at("file"),
              "file:///C:/Users/klein/AppData/Local/Temp/kleinbot/a.wav");
    EXPECT_EQ(relative.params.at("message").at(0).at("data").at("file"),
              "file:///source/voice.wav");
    EXPECT_EQ(windowsImage.params.at("message").at(0).at("data").at("file"),
              "file:///source/image_assets/img_1.jpg");
}

// transport 在投递落定后依赖该函数定位并删除语音临时文件
TEST(VoiceAttachmentPathTest, ExtractsPathOnlyForVoiceMessages)
{
    EXPECT_EQ(voiceAttachmentPath(VoiceMessage{"/tmp/kleinbot/a.wav"}),
              std::optional<std::string>{"/tmp/kleinbot/a.wav"});
    EXPECT_EQ(voiceAttachmentPath(TextMessage{"hello"}), std::nullopt);
    EXPECT_EQ(voiceAttachmentPath(
                  ImageMessage{ImageMessage::Source::LocalPath, "/tmp/x.png"}),
              std::nullopt);
    EXPECT_EQ(voiceAttachmentPath(MusicMessage{42}), std::nullopt);
}

TEST(OneBotMessageEncoderTest, RejectsTargetsUnsupportedByOneBot)
{
    OneBotMessageEncoder encoder;
    const OutboundDelivery delivery{DirectMessageTarget{"satori-user"}, TextMessage{"hello"}};

    EXPECT_THROW(encoder.encode(delivery), std::invalid_argument);
}
