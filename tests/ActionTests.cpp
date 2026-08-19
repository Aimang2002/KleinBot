#include <gtest/gtest.h>

#include "Action/GetCurrentModelAction.h"
#include "Tool/ActionTool.h"
#include "Tool/ToolArgumentParser.h"
#include "Tool/ToolRegistry.h"

namespace
{
class RecordingAction : public Action
{
public:
    const ActionDescriptor &descriptor() const override
    {
        static const ActionDescriptor value{
            "record_action",
            "records arguments and context",
            {{"type", "object"},
             {"properties", {{"enabled", {{"type", "boolean"}}}}},
             {"required", {"enabled"}}},
            true,
            true};
        return value;
    }

    ActionResult execute(const nlohmann::json &arguments,
                         const ActionContext &context) override
    {
        lastArguments = arguments;
        lastContext = context;
        return {"done", {TextMessage{"sent"}}, "[context]", true};
    }

    nlohmann::json lastArguments;
    ActionContext lastContext;
};
}

TEST(GetCurrentModelActionTest, ExposesStableToolContract)
{
    GetCurrentModelAction action([](uint64_t) { return "test-model"; });

    const auto &descriptor = action.descriptor();
    EXPECT_EQ(descriptor.name, "get_current_model");
    EXPECT_TRUE(descriptor.expose_as_tool);
    EXPECT_FALSE(descriptor.requires_admin);
    EXPECT_EQ(descriptor.parameters_schema.at("type"), "object");
    EXPECT_TRUE(descriptor.parameters_schema.at("properties").empty());
}

TEST(GetCurrentModelActionTest, UsesCurrentUserWhenQueryingModel)
{
    uint64_t queriedUser = 0;
    GetCurrentModelAction action([&queriedUser](uint64_t userId) {
        queriedUser = userId;
        return "model-a";
    });

    const auto result = action.execute(nlohmann::json::object(), {12345, 99});

    EXPECT_EQ(queriedUser, 12345U);
    EXPECT_EQ(result.content, "当前模型：model-a");
    EXPECT_TRUE(result.terminal);
}

TEST(ActionToolTest, ForwardsDescriptorArgumentsContextAndResult)
{
    RecordingAction action;
    ActionTool tool(action);

    const auto result = tool.execute(R"({"enabled":true})", {42, 88});

    EXPECT_EQ(tool.name(), "record_action");
    EXPECT_EQ(tool.description(), "records arguments and context");
    EXPECT_TRUE(tool.requiresAdmin());
    EXPECT_EQ(nlohmann::json::parse(tool.parametersSchema()),
              action.descriptor().parameters_schema);
    EXPECT_TRUE(action.lastArguments.at("enabled").get<bool>());
    EXPECT_EQ(action.lastContext.user_id, 42U);
    EXPECT_EQ(action.lastContext.user_message_id, 88);
    EXPECT_EQ(result.model_content, "done");
    ASSERT_EQ(result.outbound_messages.size(), 1U);
    EXPECT_EQ(std::get<TextMessage>(result.outbound_messages.front()).content, "sent");
    EXPECT_EQ(result.context_content, "[context]");
    EXPECT_TRUE(result.terminal);
}

TEST(ActionToolTest, ConvertsInvalidJsonIntoTerminalError)
{
    RecordingAction action;
    ActionTool tool(action);

    const auto result = tool.execute("not-json", {42, 88});

    EXPECT_NE(result.model_content.find("错误：操作参数无效"), std::string::npos);
    EXPECT_TRUE(result.outbound_messages.empty());
    EXPECT_TRUE(result.terminal);
}

TEST(ToolArgumentParserTest, AcceptsStandardJsonObject)
{
    const nlohmann::json arguments = parseToolArguments(R"({"enabled":true})");

    EXPECT_TRUE(arguments.at("enabled").get<bool>());
}

TEST(ToolArgumentParserTest, MergesConcatenatedGatewayObjects)
{
    const nlohmann::json arguments = parseToolArguments(
        R"({}{"query":"中东局势最新动态 2026","max_results":8})");

    EXPECT_EQ(arguments.at("query"), "中东局势最新动态 2026");
    EXPECT_EQ(arguments.at("max_results"), 8);
}

TEST(ToolArgumentParserTest, RejectsTrailingNonJsonContent)
{
    EXPECT_THROW(parseToolArguments(R"({"enabled":true}garbage)"), std::exception);
}

TEST(ToolArgumentParserTest, NormalizesGatewayArgumentsForEchoBack)
{
    // 网关返回的畸形参数必须在回灌前修复，否则网关无法重建 tool_use 块
    const std::string normalized = normalizeToolArguments(
        R"({}{"query":"GLM 最新版本","topic":"general"})");
    const nlohmann::json parsed = nlohmann::json::parse(normalized);

    EXPECT_EQ(parsed.at("query"), "GLM 最新版本");
    EXPECT_EQ(parsed.at("topic"), "general");

    EXPECT_EQ(normalizeToolArguments(R"({"a":1})"), R"({"a":1})");
    // 无法解析的内容原样透传，不制造新的失败
    EXPECT_EQ(normalizeToolArguments("not json"), "not json");
}

TEST(ToolResultTest, OutboundMessagesAlwaysTerminateToolRound)
{
    const ToolResult result{"sent", {ImageMessage{ImageMessage::Source::LocalPath, "/tmp/image.png"}}, {}, false};

    EXPECT_TRUE(terminatesToolRound(result));
}

TEST(ToolRegistryTest, ListsRegisteredToolNames)
{
    RecordingAction action;
    ToolRegistry registry;
    registry.registerTool(std::make_unique<ActionTool>(action));

    EXPECT_EQ(registry.names(), std::vector<std::string>{"record_action"});
}
