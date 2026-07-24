#include <gtest/gtest.h>

#include "Network/TransportConfig.h"
#include "Protocol/OneBot/OneBotEventDecoder.h"

TEST(TransportConfigTest, ReadsLegacyWebSocketKeys)
{
    const auto config = TransportConfig::fromValues({
        {"TRANSPORT_MODE", "forward_websocket"},
        {"WEBSOCKET_MESSAGE_IP", "127.0.0.1"},
        {"WEBSOCKET_MESSAGE_PORT", "9999"},
        {"REVERSEWEBSOCKET_MESSAGE_IP", "127.0.0.1"},
        {"REVERSEWEBSOCKET_MESSAGE_PORT", "8600"},
        {"WEBSOCKET_AUTH_TOKEN", "secret"}
    });

    EXPECT_EQ(config.mode, TransportMode::ForwardWebSocket);
    EXPECT_EQ(config.forwardWebSocket.host, "127.0.0.1");
    EXPECT_EQ(config.forwardWebSocket.port, "9999");
    EXPECT_EQ(config.forwardWebSocket.path, "/");
    EXPECT_EQ(config.forwardWebSocket.authToken, "secret");
    EXPECT_EQ(config.reverseWebSocket.bindPort, 8600);
}

TEST(TransportConfigTest, ValidatesHttpModeAndDefaults)
{
    const auto config = TransportConfig::fromValues({
        {"TRANSPORT_MODE", "http"},
        {"HTTP_API_BASE_URL", "https://127.0.0.1:5700"},
        {"HTTP_EVENT_BIND_PORT", "8080"}
    });

    EXPECT_EQ(config.mode, TransportMode::Http);
    EXPECT_EQ(config.http.eventBindHost, "127.0.0.1");
    EXPECT_EQ(config.http.eventPath, "/onebot/events");
    EXPECT_EQ(config.connectTimeoutMs, 5000);
    EXPECT_EQ(config.requestTimeoutMs, 15000);
    EXPECT_EQ(config.maxBodyBytes, 1048576U);
}

TEST(TransportConfigTest, RejectsInvalidModeAndPath)
{
    EXPECT_THROW(TransportConfig::fromValues({
        {"TRANSPORT_MODE", "hybrid"}
    }), std::invalid_argument);

    EXPECT_THROW(TransportConfig::fromValues({
        {"TRANSPORT_MODE", "forward_websocket"},
        {"WS_EVENT_HOST", "127.0.0.1"},
        {"WS_EVENT_PORT", "9999"},
        {"WS_EVENT_PATH", "onebot"}
    }), std::invalid_argument);
}

TEST(OneBotEventDecoderTest, ProducesProtocolIndependentInboundMessage)
{
    OneBotEventDecoder decoder;
    const std::string payload = R"({
        "post_type":"message",
        "message_type":"group",
        "user_id":42,
        "group_id":88,
        "message_id":123,
        "time":456,
        "raw_message":"hello[CQ:image]",
        "sender":{"nickname":"nick","card":"card"},
        "message":[
            {"type":"text","data":{"text":"hello"}},
            {"type":"image","data":{"url":"https://example.test/a.png"}}
        ]
    })";

    const auto message = decoder.decode(payload);

    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->user_id, 42U);
    EXPECT_EQ(message->group_id, 88U);
    EXPECT_EQ(message->plain_text, "hello");
    EXPECT_EQ(message->message_data_url, "https://example.test/a.png");
    EXPECT_EQ(message->payload_size_bytes, payload.size());
}

TEST(OneBotEventDecoderTest, IgnoresApiResponsesAndMetaEvents)
{
    OneBotEventDecoder decoder;

    EXPECT_FALSE(decoder.decode(R"({"status":"ok","retcode":0})").has_value());
    EXPECT_FALSE(decoder.decode(R"({"post_type":"meta_event"})").has_value());
}
