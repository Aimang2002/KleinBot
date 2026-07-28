#include <gtest/gtest.h>

#include "Application/CurrentImageRouting.h"
#include "ModelApiCaller/ChatPayloadBuilder.h"
#include "ModelRegistry/ModelRegistry.h"

#include <filesystem>
#include <fstream>

namespace
{
ChatImageContent sampleImage()
{
    return {"asset-1", "image/png", "aGVsbG8="};
}
}

TEST(CurrentImageRoutingTest, AttachesCurrentImageWhenModelSupportsVision)
{
    ChatRequest request;
    request.history.push_back({"user", "看一下这张图片\n[image asset_id=asset-1 source=inbound]"});
    ChatModel model;
    model.capabilities.vision = true;

    EXPECT_EQ(routeCurrentImage(request, model, sampleImage()),
              CurrentImageRoute::NativeMultimodal);
    ASSERT_EQ(request.history.back().images.size(), 1U);
    EXPECT_EQ(request.history.back().images.front().asset_id, "asset-1");
    EXPECT_EQ(request.history.back().images.front().mime_type, "image/png");
}

TEST(CurrentImageRoutingTest, LeavesRequestTextOnlyForToolFallback)
{
    ChatRequest request;
    request.history.push_back({"user", "看一下这张图片\n[image asset_id=asset-1 source=inbound]"});
    ChatModel model;

    EXPECT_EQ(routeCurrentImage(request, model, sampleImage()),
              CurrentImageRoute::ToolFallback);
    EXPECT_TRUE(request.history.back().images.empty());
}

TEST(CurrentImageRoutingTest, FallsBackWhenCurrentImageCannotBeRead)
{
    ChatRequest request;
    request.history.push_back({"user", "看一下这张图片"});
    ChatModel model;
    model.capabilities.vision = true;
    ChatImageContent unreadableImage{"asset-1", "image/png", {}};

    EXPECT_EQ(routeCurrentImage(request, model, unreadableImage),
              CurrentImageRoute::ToolFallback);
    EXPECT_TRUE(request.history.back().images.empty());
}

TEST(CurrentImageRoutingTest, RemovesNativeImagesBeforeToolFallbackRetry)
{
    ChatRequest request;
    ChatMessage first;
    first.role = "user";
    first.content = "first";
    first.images.push_back(sampleImage());
    ChatMessage second;
    second.role = "assistant";
    second.content = "second";
    request.history.push_back(first);
    request.history.push_back(second);

    EXPECT_EQ(removeRequestImages(request), 1U);
    EXPECT_TRUE(request.history.front().images.empty());
}

TEST(ChatPayloadBuilderTest, EncodesOpenAIMultimodalUserContent)
{
    ChatRequest request;
    request.system_prompt = "system";
    ChatMessage message;
    message.role = "user";
    message.content = "描述图片";
    message.images.push_back(sampleImage());
    request.history.push_back(message);

    const nlohmann::json payload = ChatPayloadBuilder::openAI("vision-model", request);
    const auto &content = payload.at("messages").at(1).at("content");

    ASSERT_TRUE(content.is_array());
    EXPECT_EQ(content.at(0).at("type"), "text");
    EXPECT_EQ(content.at(0).at("text"), "描述图片");
    EXPECT_EQ(content.at(1).at("type"), "image_url");
    EXPECT_EQ(content.at(1).at("image_url").at("url"),
              "data:image/png;base64,aGVsbG8=");
}

TEST(ChatPayloadBuilderTest, EncodesAnthropicMultimodalUserContent)
{
    ChatRequest request;
    request.system_prompt = "system";
    ChatMessage message;
    message.role = "user";
    message.content = "描述图片";
    message.images.push_back(sampleImage());
    request.history.push_back(message);

    const nlohmann::json payload = ChatPayloadBuilder::anthropic(
        "vision-model", request, 4096);
    const auto &content = payload.at("messages").at(0).at("content");

    ASSERT_TRUE(content.is_array());
    EXPECT_EQ(content.at(0).at("type"), "image");
    EXPECT_EQ(content.at(0).at("source").at("media_type"), "image/png");
    EXPECT_EQ(content.at(0).at("source").at("data"), "aGVsbG8=");
    EXPECT_EQ(content.at(1).at("type"), "text");
    EXPECT_EQ(content.at(1).at("text"), "描述图片");
}

TEST(ChatPayloadBuilderTest, DetectsExplicitTextOnlyApiRejection)
{
    const std::string response = R"({
        "error": {
            "code": "1210",
            "message": "messages.content.type 参数非法，取值范围 ['text']"
        }
    })";

    EXPECT_TRUE(ChatPayloadBuilder::explicitlyRejectsMultimodal(400, response));
    EXPECT_FALSE(ChatPayloadBuilder::explicitlyRejectsMultimodal(
        504, R"({"error":{"message":"模型请求超时"}})"));
    EXPECT_FALSE(ChatPayloadBuilder::explicitlyRejectsMultimodal(
        401, R"({"error":{"message":"invalid api key"}})"));
}

TEST(ModelRegistryTest, ReadsOptionalVisionCapabilityWithoutBreakingLegacyEntries)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "kleinbot-model-capabilities.json";
    {
        std::ofstream output(path);
        output << R"({
            "Models": [
                {
                    "ModelName": ["vision-model"],
                    "api_key": "key",
                    "api_endpoint": "https://example.test/chat",
                    "APIStandard": "OpenAI",
                    "Capabilities": {"vision": true}
                },
                {
                    "ModelName": ["text-model"],
                    "api_key": "key",
                    "api_endpoint": "https://example.test/chat",
                    "APIStandard": "OpenAI"
                }
            ]
        })";
    }

    ModelRegistry registry(path.string());
    const ChatModel *visionModel = registry.find("vision-model");
    const ChatModel *textModel = registry.find("text-model");

    ASSERT_NE(visionModel, nullptr);
    ASSERT_NE(textModel, nullptr);
    EXPECT_TRUE(visionModel->capabilities.vision);
    EXPECT_FALSE(textModel->capabilities.vision);
    std::filesystem::remove(path);
}
