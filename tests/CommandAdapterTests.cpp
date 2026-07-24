#include <gtest/gtest.h>

#include "Action/Action.h"
#include "Command/AdminCommand.h"
#include "Command/QueryModelCommand.h"
#include "Command/VoiceSwitchCommand.h"

namespace
{
class RecordingAction : public Action
{
public:
    explicit RecordingAction(bool requiresAdmin = false)
        : requiresAdminValue(requiresAdmin)
    {
    }

    const ActionDescriptor &descriptor() const override
    {
        descriptorValue.requires_admin = requiresAdminValue;
        return descriptorValue;
    }

    ActionResult execute(const nlohmann::json &arguments,
                         const ActionContext &context) override
    {
        lastArguments = arguments;
        lastContext = context;
        return {"action-result", {}, {}, true};
    }

    mutable ActionDescriptor descriptorValue{
        "record", "record", nlohmann::json::object(), false, false};
    bool requiresAdminValue;
    nlohmann::json lastArguments;
    ActionContext lastContext;
};

CommandContext makeContext(InboundMessage &data, uint64_t userId)
{
    data.user_id = userId;
    data.group_id = 100;
    data.message_type = "group";
    return {userId, data.group_id, data.message_type, data};
}

std::string textContent(const CommandResult &result)
{
    return std::get<TextMessage>(result.payload).content;
}
}

TEST(QueryModelCommandTest, MapsExplicitCommandToAction)
{
    RecordingAction action;
    QueryModelCommand command(action);
    InboundMessage data;
    data.plain_text = "#查询当前模型";
    auto context = makeContext(data, 123);

    EXPECT_TRUE(command.canHandle("#查询当前模型"));
    EXPECT_FALSE(command.canHandle("当前是什么模型"));

    const auto result = command.execute(context);

    EXPECT_TRUE(action.lastArguments.empty());
    EXPECT_EQ(action.lastContext.user_id, 123U);
    EXPECT_EQ(textContent(result), "action-result");
}

TEST(VoiceSwitchCommandTest, MapsEnableAndDisableArguments)
{
    RecordingAction action;
    VoiceSwitchCommand command(action);
    InboundMessage data;
    auto context = makeContext(data, 321);

    data.plain_text = "#开启语音";
    command.execute(context);
    EXPECT_TRUE(action.lastArguments.at("enabled").get<bool>());

    data.plain_text = "#关闭语音";
    command.execute(context);
    EXPECT_FALSE(action.lastArguments.at("enabled").get<bool>());
}

TEST(AdminCommandTest, PreservesAdminRequirementAndMapsOperation)
{
    RecordingAction action(true);
    AdminCommand command(action);
    InboundMessage data;
    data.plain_text = "#刷新配置文件";
    auto context = makeContext(data, 999);

    EXPECT_TRUE(command.canHandle("#刷新配置文件"));
    EXPECT_TRUE(command.requiresAdmin());

    const auto result = command.execute(context);

    EXPECT_EQ(action.lastArguments.at("action"), "refresh_config");
    EXPECT_EQ(action.lastContext.user_id, 999U);
    EXPECT_EQ(textContent(result), "action-result");
}
