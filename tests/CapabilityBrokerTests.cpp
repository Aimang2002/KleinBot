#include <gtest/gtest.h>

#include "Application/CapabilityBroker.h"

#include <string>
#include <vector>

TEST(BotFeatureTableTest, StaticTableRowsMatchProtocolMatrix)
{
    // NapCat：全支持
    EXPECT_TRUE(featureSupported("NapCat", BotFeature::InputStatus));
    EXPECT_TRUE(featureSupported("NapCat 4.8.2", BotFeature::GroupPoke));
    // LLOneBot：输入状态存疑按不支持，戳一戳支持
    EXPECT_FALSE(featureSupported("LLOneBot 1.3.0", BotFeature::InputStatus));
    EXPECT_TRUE(featureSupported("LLOneBot", BotFeature::GroupPoke));
    // Lagrange：发送动作名不同，本版本全关（roadmap §2 决策）
    EXPECT_FALSE(featureSupported("Lagrange.OneBot", BotFeature::InputStatus));
    EXPECT_FALSE(featureSupported("Lagrange.OneBot", BotFeature::GroupPoke));
}

TEST(BotFeatureTableTest, UnknownImplementationIsConservativelyDisabled)
{
    // D12 保守默认：未列出的实现端全部 false，功能静默关闭
    EXPECT_FALSE(featureSupported("SomeNewBot", BotFeature::InputStatus));
    EXPECT_FALSE(featureSupported("SomeNewBot", BotFeature::GroupPoke));
    EXPECT_FALSE(featureSupported("", BotFeature::GroupPoke));
}

TEST(BotFeatureTableTest, MatchingIsCaseInsensitive)
{
    EXPECT_TRUE(featureSupported("napcat-shell", BotFeature::GroupPoke));
    EXPECT_TRUE(featureSupported("LLONEBOT", BotFeature::GroupPoke));
}

TEST(AppNameFromInfoTest, ExtractsAppNameFromOkResponse)
{
    OneBotApiResult ok;
    ok.retcode = 0;
    ok.data = nlohmann::json({{"app_name", "NapCat"}, {"app_version", "4.8.2"}});
    const auto name = appNameFromInfo(ok);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "NapCat");
}

TEST(AppNameFromInfoTest, RejectsFailedOrMalformedResponses)
{
    OneBotApiResult networkFailure;
    networkFailure.networkError = true;
    EXPECT_FALSE(appNameFromInfo(networkFailure).has_value());

    OneBotApiResult failed;
    failed.retcode = 1404;
    failed.data = nlohmann::json::object();
    EXPECT_FALSE(appNameFromInfo(failed).has_value());

    OneBotApiResult noData;
    noData.retcode = 0;
    EXPECT_FALSE(appNameFromInfo(noData).has_value());

    OneBotApiResult emptyName;
    emptyName.retcode = 0;
    emptyName.data = nlohmann::json({{"app_name", ""}});
    EXPECT_FALSE(appNameFromInfo(emptyName).has_value());
}

namespace
{
// 可编排的假探测源：返回队首结果并计数
class ScriptedProbe
{
public:
    explicit ScriptedProbe(std::vector<std::optional<OneBotApiResult>> script)
        : script_(std::move(script)) {}

    std::optional<OneBotApiResult> operator()()
    {
        ++calls;
        if (resultsReturned >= script_.size())
            return std::nullopt;
        return script_[resultsReturned++];
    }

    std::size_t calls = 0;

private:
    std::vector<std::optional<OneBotApiResult>> script_;
    std::size_t resultsReturned = 0;
};

OneBotApiResult napCatInfo()
{
    OneBotApiResult result;
    result.status = "ok";
    result.retcode = 0;
    result.data = nlohmann::json({{"app_name", "NapCat"}});
    return result;
}
}

TEST(CapabilityBrokerTest, ProbesOnceAndCachesFeatureBits)
{
    ScriptedProbe probe({napCatInfo()});
    CapabilityBroker broker(std::ref(probe));

    // 未探测：保守 false
    EXPECT_FALSE(broker.supports(BotFeature::InputStatus));
    EXPECT_TRUE(broker.endpointSummary().empty());

    broker.poll(100);

    EXPECT_TRUE(broker.supports(BotFeature::InputStatus));
    EXPECT_TRUE(broker.supports(BotFeature::GroupPoke));
    EXPECT_EQ(broker.endpointSummary(), "NapCat");
    EXPECT_EQ(probe.calls, 1u);

    // 就绪后 poll 不再探测（终态缓存，重连不重探）
    broker.poll(100000);
    EXPECT_EQ(probe.calls, 1u);
}

TEST(CapabilityBrokerTest, FailureEntersCooldownThenRetries)
{
    OneBotApiResult failure;
    failure.networkError = true;
    ScriptedProbe probe({failure, napCatInfo()});
    CapabilityBroker broker(std::ref(probe));

    broker.poll(100);
    EXPECT_EQ(probe.calls, 1u);
    EXPECT_FALSE(broker.supports(BotFeature::GroupPoke));

    // 冷却期内不重试
    broker.poll(130);
    EXPECT_EQ(probe.calls, 1u);

    // 冷却结束重试成功
    broker.poll(161);
    EXPECT_EQ(probe.calls, 2u);
    EXPECT_TRUE(broker.supports(BotFeature::GroupPoke));
    EXPECT_EQ(broker.endpointSummary(), "NapCat");
}

TEST(CapabilityBrokerTest, UnknownImplementationYieldsAllDisabled)
{
    OneBotApiResult unknown;
    unknown.retcode = 0;
    unknown.data = nlohmann::json({{"app_name", "MysteryBot"}});
    ScriptedProbe probe({unknown});
    CapabilityBroker broker(std::ref(probe));

    broker.poll(10);

    EXPECT_FALSE(broker.supports(BotFeature::InputStatus));
    EXPECT_FALSE(broker.supports(BotFeature::GroupPoke));
    EXPECT_EQ(broker.endpointSummary(), "MysteryBot");
}
