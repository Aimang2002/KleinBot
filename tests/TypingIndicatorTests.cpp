#include <gtest/gtest.h>

#include "Application/TypingIndicator.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
class FakeApiChannel final : public OneBotApiChannel
{
public:
    OneBotApiResult call(const std::string &action, nlohmann::json params,
                         std::chrono::milliseconds) override
    {
        ++calls;
        actions.push_back(action);
        paramsList.push_back(std::move(params));
        return result;
    }

    std::size_t calls = 0;
    std::vector<std::string> actions;
    std::vector<nlohmann::json> paramsList;
    OneBotApiResult result;
};

CapabilityBroker::VersionProbe probeFor(const std::string &appName)
{
    auto info = std::make_shared<OneBotApiResult>();
    info->retcode = 0;
    info->data = nlohmann::json({{"app_name", appName}});
    return [info]() -> std::optional<OneBotApiResult> { return *info; };
}
}

TEST(TypingIndicatorTest, SendsInputStatusWhenCapabilitySupported)
{
    CapabilityBroker broker(probeFor("NapCat"));
    broker.poll(0);
    FakeApiChannel api;
    TypingIndicator indicator(broker, api);

    indicator.begin(42);

    ASSERT_EQ(api.calls, 1u);
    EXPECT_EQ(api.actions[0], "set_input_status");
    EXPECT_EQ(api.paramsList[0].value("user_id", 0ULL), 42ULL);
}

TEST(TypingIndicatorTest, SilentWhenCapabilityMissingOrNotReady)
{
    // LLOneBot：GroupPoke 开但 InputStatus 关
    CapabilityBroker llonebot(probeFor("LLOneBot"));
    llonebot.poll(0);
    FakeApiChannel api;
    TypingIndicator indicator(llonebot, api);
    indicator.begin(42);
    EXPECT_EQ(api.calls, 0u);

    // 未探测（NotReady 状态）：保守关闭
    CapabilityBroker neverProbed(
        []() -> std::optional<OneBotApiResult> { return std::nullopt; });
    TypingIndicator notReady(neverProbed, api);
    notReady.begin(42);
    EXPECT_EQ(api.calls, 0u);
}

TEST(TypingIndicatorTest, FailureResultIsIgnoredSilently)
{
    CapabilityBroker broker(probeFor("NapCat"));
    broker.poll(0);
    FakeApiChannel api;
    api.result.networkError = true;
    TypingIndicator indicator(broker, api);

    EXPECT_NO_THROW(indicator.begin(7));
    EXPECT_EQ(api.calls, 1u);
}
