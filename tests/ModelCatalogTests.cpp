#include <gtest/gtest.h>

#include "WebUI/ModelCatalog.h"

#include <string>

TEST(ModelCatalogTest, DerivesModelsUrlFromChatCompletionsEndpoint)
{
    EXPECT_EQ(ModelCatalog::deriveModelsUrl("https://api.deepseek.com/v1/chat/completions"),
              "https://api.deepseek.com/v1/models");
}

TEST(ModelCatalogTest, DerivesModelsUrlFromAnthropicMessagesEndpoint)
{
    EXPECT_EQ(ModelCatalog::deriveModelsUrl("https://fast.example.com/v1/messages"),
              "https://fast.example.com/v1/models");
}

TEST(ModelCatalogTest, DerivesModelsUrlTrimsTrailingSlash)
{
    EXPECT_EQ(ModelCatalog::deriveModelsUrl("https://api.example.com/v1/chat/completions/"),
              "https://api.example.com/v1/models");
}

TEST(ModelCatalogTest, KeepsEndpointAlreadyPointingAtModels)
{
    EXPECT_EQ(ModelCatalog::deriveModelsUrl("https://api.example.com/v1/models"),
              "https://api.example.com/v1/models");
}

TEST(ModelCatalogTest, RejectsNonHttpSchemeAndUnknownSuffix)
{
    EXPECT_FALSE(ModelCatalog::deriveModelsUrl("ftp://api.example.com/v1/models").has_value());
    EXPECT_FALSE(ModelCatalog::deriveModelsUrl("api.example.com/v1/chat/completions").has_value());
    EXPECT_FALSE(ModelCatalog::deriveModelsUrl("https://api.example.com/v1/foo/bar").has_value());
    EXPECT_FALSE(ModelCatalog::deriveModelsUrl("").has_value());
}

TEST(ModelCatalogTest, ParsesOpenAiAndAnthropicDataResponse)
{
    std::string error;
    const std::vector<std::string> models = ModelCatalog::parseModelsResponse(
        R"({"data": [{"id": "gpt-b"}, {"id": "gpt-a"}, {"id": "gpt-a"}]})", error);
    EXPECT_TRUE(error.empty());
    // 去重并按名称排序
    EXPECT_EQ(models, (std::vector<std::string>{"gpt-a", "gpt-b"}));
}

TEST(ModelCatalogTest, ParsesPlainArrayResponse)
{
    std::string error;
    const std::vector<std::string> models = ModelCatalog::parseModelsResponse(
        R"(["model-x", {"id": "model-y"}])", error);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(models, (std::vector<std::string>{"model-x", "model-y"}));
}

TEST(ModelCatalogTest, SkipsEntriesWithoutUsableName)
{
    std::string error;
    const std::vector<std::string> models = ModelCatalog::parseModelsResponse(
        R"({"data": [{"object": "model"}, {"id": 42}, {"id": "keep-me"}]})", error);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(models, (std::vector<std::string>{"keep-me"}));
}

TEST(ModelCatalogTest, RejectsMalformedJsonAndMissingData)
{
    std::string error;
    EXPECT_TRUE(ModelCatalog::parseModelsResponse("not-json", error).empty());
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_TRUE(ModelCatalog::parseModelsResponse(R"({"object": "list"})", error).empty());
    EXPECT_FALSE(error.empty());
}

TEST(ModelCatalogTest, ClassifiesVisionProbeOutcomes)
{
    using MR = ModelCatalog::VisionProbe;

    // 2xx：支持
    EXPECT_EQ(ModelCatalog::classifyVisionProbe(200, R"({"choices":[]})"), MR::Vision);

    // 4xx 且报错指向图片/多模态不支持 → 明确不支持
    EXPECT_EQ(ModelCatalog::classifyVisionProbe(400,
              R"({"error":{"message":"Image input is not supported for this model"}})"),
              MR::NoVision);
    EXPECT_EQ(ModelCatalog::classifyVisionProbe(400,
              R"({"error":{"message":"model does not support multimodal content"}})"),
              MR::NoVision);

    // 中文供应商的“仅文本”拒绝形态（智谱 1210）→ 明确不支持
    EXPECT_EQ(ModelCatalog::classifyVisionProbe(400,
              R"({"error":{"code":"1210","message":"messages.content.type 参数非法，取值范围 ['text']"}})"),
              MR::NoVision);
    EXPECT_EQ(ModelCatalog::classifyVisionProbe(400,
              R"({"error":{"message":"该模型不支持图片输入"}})"),
              MR::NoVision);

    // 允许值枚举里含 image：图片形态本身被接受，400 另有原因 → 无法判定
    EXPECT_EQ(ModelCatalog::classifyVisionProbe(400,
              R"({"error":{"message":"content.type 参数非法，取值范围 ['text','image_url']"}})"),
              MR::Unknown);
    EXPECT_EQ(ModelCatalog::classifyVisionProbe(400,
              R"({"error":{"message":"content type must be one of ['text', 'image_url']"}})"),
              MR::Unknown);

    // 4xx 但错误与能力无关（模型不存在/鉴权/参数）→ 无法判定
    EXPECT_EQ(ModelCatalog::classifyVisionProbe(400,
              R"({"error":{"message":"model not found"}})"), MR::Unknown);
    EXPECT_EQ(ModelCatalog::classifyVisionProbe(401, R"({"error":"invalid api key"})"),
              MR::Unknown);

    // 5xx / 网络层失败没有状态码结论 → 无法判定
    EXPECT_EQ(ModelCatalog::classifyVisionProbe(500, "internal error"), MR::Unknown);
    EXPECT_EQ(ModelCatalog::classifyVisionProbe(0, ""), MR::Unknown);
}
