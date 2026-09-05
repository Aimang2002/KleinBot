#include <gtest/gtest.h>

#include "Application/CurrentImageRouting.h"
#include "ModelApiCaller/AnthropicStandard/AnthropicStandard.h"
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
    model.visionModels = {"vision-model"};

    EXPECT_EQ(routeCurrentImage(request, model, "vision-model", sampleImage()),
              CurrentImageRoute::NativeMultimodal);
    ASSERT_EQ(request.history.back().images.size(), 1U);
    EXPECT_EQ(request.history.back().images.front().asset_id, "asset-1");
    EXPECT_EQ(request.history.back().images.front().mime_type, "image/png");
}

TEST(CurrentImageRoutingTest, VisionIsPerModelInsideSharedGroup)
{
    ChatRequest request;
    request.history.push_back({"user", "看一下这张图片\n[image asset_id=asset-1 source=inbound]"});
    ChatModel model;
    model.visionModels = {"vision-model"};

    // 同组内的无视觉模型不得继承标注
    EXPECT_EQ(routeCurrentImage(request, model, "text-model", sampleImage()),
              CurrentImageRoute::ToolFallback);
    EXPECT_TRUE(request.history.back().images.empty());
}

TEST(CurrentImageRoutingTest, LeavesRequestTextOnlyForToolFallback)
{
    ChatRequest request;
    request.history.push_back({"user", "看一下这张图片\n[image asset_id=asset-1 source=inbound]"});
    ChatModel model;

    EXPECT_EQ(routeCurrentImage(request, model, "any-model", sampleImage()),
              CurrentImageRoute::ToolFallback);
    EXPECT_TRUE(request.history.back().images.empty());
}

TEST(CurrentImageRoutingTest, FallsBackWhenCurrentImageCannotBeRead)
{
    ChatRequest request;
    request.history.push_back({"user", "看一下这张图片"});
    ChatModel model;
    model.visionModels = {"vision-model"};
    ChatImageContent unreadableImage{"asset-1", "image/png", {}};

    EXPECT_EQ(routeCurrentImage(request, model, "vision-model", unreadableImage),
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

TEST(ChatPayloadBuilderTest, EncodesEmptyToolCallPreambleAsNull)
{
    ChatRequest request;
    ChatMessage assistant;
    assistant.role = "assistant";
    assistant.tool_calls.push_back({"call-1", "web_search", R"({"query":"news"})"});
    request.history.push_back(std::move(assistant));

    const nlohmann::json payload = ChatPayloadBuilder::openAI("test-model", request);

    ASSERT_EQ(payload.at("messages").size(), 1U);
    EXPECT_TRUE(payload.at("messages").at(0).at("content").is_null());
    EXPECT_EQ(payload.at("messages").at(0).at("tool_calls").at(0)
                  .at("function").at("name"),
              "web_search");
}

TEST(ChatPayloadBuilderTest, ConvertsOpenAIToolSchemasToAnthropicTools)
{
    ChatRequest request;
    request.tools = {R"({"type":"function","function":{"name":"inspect_image","description":"查看图片","parameters":{"type":"object","properties":{"asset_id":{"type":"string"}},"required":["asset_id"]}}})"};

    const nlohmann::json payload = ChatPayloadBuilder::anthropic(
        "claude-model", request, 4096);

    ASSERT_TRUE(payload.contains("tools"));
    ASSERT_EQ(payload.at("tools").size(), 1U);
    EXPECT_EQ(payload.at("tools").at(0).at("name"), "inspect_image");
    EXPECT_EQ(payload.at("tools").at(0).at("description"), "查看图片");
    EXPECT_EQ(payload.at("tools").at(0).at("input_schema").at("required").at(0),
              "asset_id");
    EXPECT_FALSE(payload.at("tools").at(0).contains("function"));
}

TEST(ChatPayloadBuilderTest, OmitsAnthropicToolsKeyWhenRequestHasNoTools)
{
    ChatRequest request;
    request.history.push_back({"user", "hi"});

    const nlohmann::json payload = ChatPayloadBuilder::anthropic(
        "claude-model", request, 4096);

    EXPECT_FALSE(payload.contains("tools"));
}

TEST(ChatPayloadBuilderTest, EncodesAnthropicAssistantToolCallsAsToolUseBlocks)
{
    ChatRequest request;
    ChatMessage assistant;
    assistant.role = "assistant";
    assistant.content = "我先看一下图片";
    assistant.tool_calls.push_back({"call-1", "inspect_image", R"({"asset_id":"asset-1"})"});
    request.history.push_back(std::move(assistant));

    const nlohmann::json payload = ChatPayloadBuilder::anthropic(
        "claude-model", request, 4096);

    ASSERT_EQ(payload.at("messages").size(), 1U);
    const auto &content = payload.at("messages").at(0).at("content");
    ASSERT_TRUE(content.is_array());
    ASSERT_EQ(content.size(), 2U);
    EXPECT_EQ(content.at(0).at("type"), "text");
    EXPECT_EQ(content.at(0).at("text"), "我先看一下图片");
    EXPECT_EQ(content.at(1).at("type"), "tool_use");
    EXPECT_EQ(content.at(1).at("id"), "call-1");
    EXPECT_EQ(content.at(1).at("name"), "inspect_image");
    EXPECT_EQ(content.at(1).at("input").at("asset_id"), "asset-1");
}

TEST(ChatPayloadBuilderTest, FallsBackToEmptyInputOnCorruptedToolArguments)
{
    ChatRequest request;
    ChatMessage assistant;
    assistant.role = "assistant";
    assistant.tool_calls.push_back({"call-1", "inspect_image", "{\"asset_id\": broken"});
    request.history.push_back(std::move(assistant));

    const nlohmann::json payload = ChatPayloadBuilder::anthropic(
        "claude-model", request, 4096);

    const auto &input = payload.at("messages").at(0).at("content").at(0).at("input");
    EXPECT_TRUE(input.is_object());
    EXPECT_TRUE(input.empty());
}

TEST(ChatPayloadBuilderTest, MergesConsecutiveToolResultsIntoSingleAnthropicUserMessage)
{
    ChatRequest request;
    ChatMessage user;
    user.role = "user";
    user.content = "看图并告诉我时间";
    ChatMessage assistant;
    assistant.role = "assistant";
    assistant.tool_calls.push_back({"call-1", "inspect_image", R"({"asset_id":"asset-1"})"});
    assistant.tool_calls.push_back({"call-2", "get_time", "{}"});
    ChatMessage first;
    first.role = "tool";
    first.tool_call_id = "call-1";
    first.content = "图片内容描述";
    ChatMessage second;
    second.role = "tool";
    second.tool_call_id = "call-2";
    second.content = "当前时间 12:00";
    request.history.push_back(std::move(user));
    request.history.push_back(std::move(assistant));
    request.history.push_back(std::move(first));
    request.history.push_back(std::move(second));

    const nlohmann::json payload = ChatPayloadBuilder::anthropic(
        "claude-model", request, 4096);

    ASSERT_EQ(payload.at("messages").size(), 3U);
    const auto &results = payload.at("messages").at(2);
    EXPECT_EQ(results.at("role"), "user");
    ASSERT_TRUE(results.at("content").is_array());
    ASSERT_EQ(results.at("content").size(), 2U);
    EXPECT_EQ(results.at("content").at(0).at("type"), "tool_result");
    EXPECT_EQ(results.at("content").at(0).at("tool_use_id"), "call-1");
    EXPECT_EQ(results.at("content").at(0).at("content"), "图片内容描述");
    EXPECT_EQ(results.at("content").at(1).at("type"), "tool_result");
    EXPECT_EQ(results.at("content").at(1).at("tool_use_id"), "call-2");
    EXPECT_EQ(results.at("content").at(1).at("content"), "当前时间 12:00");
}

TEST(ChatPayloadBuilderTest, AddsAnthropicCacheBreakpointsOnToolsAndSystem)
{
    ChatRequest request;
    request.system_prompt = "system";
    request.tools = {
        R"({"type":"function","function":{"name":"tool_a","description":"a","parameters":{"type":"object"}}})",
        R"({"type":"function","function":{"name":"tool_b","description":"b","parameters":{"type":"object"}}})"
    };
    request.history.push_back({"user", "hi"});

    const nlohmann::json payload = ChatPayloadBuilder::anthropic(
        "claude-model", request, 4096);

    ASSERT_TRUE(payload.at("system").is_array());
    EXPECT_EQ(payload.at("system").at(0).at("text"), "system");
    EXPECT_EQ(payload.at("system").at(0).at("cache_control").at("type"),
              "ephemeral");
    EXPECT_FALSE(payload.at("tools").at(0).contains("cache_control"));
    EXPECT_EQ(payload.at("tools").at(1).at("cache_control").at("type"),
              "ephemeral");
    // 单条消息时只有末条断点，纯文本消息转为块数组携带断点
    const auto &content = payload.at("messages").at(0).at("content");
    ASSERT_TRUE(content.is_array());
    EXPECT_EQ(content.at(0).at("text"), "hi");
    EXPECT_EQ(content.at(0).at("cache_control").at("type"), "ephemeral");
}

TEST(ChatPayloadBuilderTest, PlacesAnthropicMessageBreakpointsOnThirdFromLastAndLast)
{
    ChatRequest request;
    request.history.push_back({"user", "第一句"});
    request.history.push_back({"assistant", "第二句"});
    request.history.push_back({"user", "第三句"});
    request.history.push_back({"assistant", "第四句"});
    request.history.push_back({"user", "最后一句"});

    const nlohmann::json payload = ChatPayloadBuilder::anthropic(
        "claude-model", request, 4096);

    const auto &messages = payload.at("messages");
    ASSERT_EQ(messages.size(), 5U);
    // 倒数第 3 条（第三句）转为块数组并在文本块上带断点
    const auto &thirdFromLast = messages.at(2).at("content");
    ASSERT_TRUE(thirdFromLast.is_array());
    EXPECT_EQ(thirdFromLast.at(0).at("text"), "第三句");
    EXPECT_EQ(thirdFromLast.at(0).at("cache_control").at("type"), "ephemeral");
    // 最后一条同样带断点
    const auto &lastContent = messages.at(4).at("content");
    ASSERT_TRUE(lastContent.is_array());
    EXPECT_EQ(lastContent.at(0).at("text"), "最后一句");
    EXPECT_EQ(lastContent.at(0).at("cache_control").at("type"), "ephemeral");
    // 其余消息不带断点、保持字符串编码
    EXPECT_TRUE(messages.at(0).at("content").is_string());
    EXPECT_TRUE(messages.at(1).at("content").is_string());
    EXPECT_TRUE(messages.at(3).at("content").is_string());
}

TEST(ChatPayloadBuilderTest, MergesConsecutiveAnthropicUserMessages)
{
    ChatRequest request;
    ChatMessage user;
    user.role = "user";
    user.content = "看图";
    ChatMessage assistant;
    assistant.role = "assistant";
    assistant.tool_calls.push_back({"call-1", "inspect_image", "{}"});
    ChatMessage tool;
    tool.role = "tool";
    tool.tool_call_id = "call-1";
    tool.content = "图片描述";
    ChatMessage wrapUp;
    wrapUp.role = "user";
    wrapUp.content = "[系统注] 工具调用轮次已达上限，不得再调用任何工具。";
    request.history.push_back(std::move(user));
    request.history.push_back(std::move(assistant));
    request.history.push_back(std::move(tool));
    request.history.push_back(std::move(wrapUp));

    const nlohmann::json payload = ChatPayloadBuilder::anthropic(
        "claude-model", request, 4096);

    // 收尾注记并入 tool_result 所在的 user 消息，保持 user/assistant 交替
    const auto &messages = payload.at("messages");
    ASSERT_EQ(messages.size(), 3U);
    EXPECT_EQ(messages.at(0).at("role"), "user");
    EXPECT_EQ(messages.at(1).at("role"), "assistant");
    EXPECT_EQ(messages.at(2).at("role"), "user");
    const auto &merged = messages.at(2).at("content");
    ASSERT_TRUE(merged.is_array());
    ASSERT_EQ(merged.size(), 2U);
    EXPECT_EQ(merged.at(0).at("type"), "tool_result");
    EXPECT_EQ(merged.at(1).at("type"), "text");
    EXPECT_NE(merged.at(1).at("text").get<std::string>().find("不得再调用任何工具"),
              std::string::npos);
}

TEST(AnthropicStandardTest, ParsesToolUseBlocksAndMapsStopReason)
{
    AnthropicStandard standard;
    const std::string raw = R"({
        "id": "msg_1",
        "type": "message",
        "role": "assistant",
        "content": [
            {"type": "text", "text": "我来看一下"},
            {"type": "tool_use", "id": "toolu_1", "name": "inspect_image",
             "input": {"asset_id": "asset-1"}}
        ],
        "model": "claude-model",
        "stop_reason": "tool_use"
    })";

    const ChatResponse response = standard.chat_json_parse(raw);

    EXPECT_EQ(response.finish_reason, "tool_calls");
    EXPECT_EQ(response.content, "我来看一下");
    ASSERT_EQ(response.tool_calls.size(), 1U);
    EXPECT_EQ(response.tool_calls[0].id, "toolu_1");
    EXPECT_EQ(response.tool_calls[0].name, "inspect_image");
    EXPECT_EQ(response.tool_calls[0].arguments, R"({"asset_id":"asset-1"})");
}

TEST(AnthropicStandardTest, ParsesPlainTextResponseWithoutToolCalls)
{
    AnthropicStandard standard;
    const std::string raw = R"({
        "id": "msg_2",
        "type": "message",
        "role": "assistant",
        "content": [{"type": "text", "text": "普通回答"}],
        "model": "claude-model",
        "stop_reason": "end_turn"
    })";

    const ChatResponse response = standard.chat_json_parse(raw);

    EXPECT_EQ(response.finish_reason, "end_turn");
    EXPECT_EQ(response.content, "普通回答");
    EXPECT_TRUE(response.tool_calls.empty());
}

TEST(ModelRegistryTest, ReadsVisionCapabilityInLegacyAndPerModelForms)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "kleinbot-model-capabilities.json";
    {
        std::ofstream output(path);
        output << R"({
            "Models": [
                {
                    "ModelName": ["legacy-a", "legacy-b"],
                    "api_key": "key",
                    "api_endpoint": "https://example.test/chat",
                    "APIStandard": "OpenAI",
                    "Capabilities": {"vision": true}
                },
                {
                    "ModelName": ["array-vision", "array-text"],
                    "api_key": "key",
                    "api_endpoint": "https://example.test/chat",
                    "APIStandard": "OpenAI",
                    "Capabilities": {"vision": ["array-vision"]}
                },
                {
                    "ModelName": ["plain-model"],
                    "api_key": "key",
                    "api_endpoint": "https://example.test/chat",
                    "APIStandard": "OpenAI",
                    "Capabilities": {"vision": "yes"}
                }
            ]
        })";
    }

    ModelRegistry registry(path.string());

    // 旧版布尔：整组继承
    const std::optional<ChatModel> legacy = registry.find("legacy-a");
    ASSERT_TRUE(legacy.has_value());
    EXPECT_TRUE(legacy->hasVision("legacy-a"));
    EXPECT_TRUE(legacy->hasVision("legacy-b"));

    // 新版数组：按模型标注，同组未列出的不带视觉
    const std::optional<ChatModel> perModel = registry.find("array-text");
    ASSERT_TRUE(perModel.has_value());
    EXPECT_TRUE(perModel->hasVision("array-vision"));
    EXPECT_FALSE(perModel->hasVision("array-text"));

    // 非法类型宽容处理：按无视觉加载，不影响整份注册表
    const std::optional<ChatModel> plain = registry.find("plain-model");
    ASSERT_TRUE(plain.has_value());
    EXPECT_FALSE(plain->hasVision("plain-model"));
    std::filesystem::remove(path);
}
