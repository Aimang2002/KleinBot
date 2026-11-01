#include <gtest/gtest.h>

#include "Affect/DecisionStore.h"
#include "Affect/JudgePolicy.h"
#include "Application/AtReplySituation.h"
#include "Application/ShadowDecision.h"
#include "utils/UniqueId.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : directory((std::filesystem::temp_directory_path() /
                     ("kleinbot-shadow-" + utils::uniqueId("t")))
                        .string())
    {
        std::filesystem::create_directories(directory);
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
    const std::string &path() const { return directory; }

private:
    std::string directory;
};

// 记录调用顺序的假策略（非基线）
class OrderedPolicy : public affect::DecisionPolicy
{
public:
    explicit OrderedPolicy(std::vector<std::string> &events) : events(events) {}
    affect::Decision decide(const affect::SituationCard &, const std::string &) override
    {
        events.push_back("decide");
        affect::Decision decision;
        decision.plan.action = "reply";
        decision.latencyMs = 7;
        return decision;
    }
    std::string version() const override { return "test_v1"; }
    std::string planVersion() const override { return "test_plan_v1"; }

private:
    std::vector<std::string> &events;
};

struct ShadowFixture
{
    TemporaryDirectory directory;
    affect::DecisionStore store{directory.path() + "/affect.db"};
    affect::DecisionRecorder recorder{&store, [] { return std::int64_t{1000}; }};
    affect::SituationCard card;
    affect::ExecutionRecord execution;

    ShadowFixture()
    {
        InboundMessage message;
        message.message_type = "group";
        message.group_id = 1026024045;
        message.user_id = 1477646;
        card = atReplySituation(message, "", {"inspect_image"}, 16);
        execution.executionId = affect::DecisionRecorder::newExecutionId();
        execution.status = "queued";
        execution.actualText = "她的回复";
    }
};

// 方案 A 的三件事：执行行在投递前已落库、决策行与执行行同一 decision_id、decide 在投递之后
TEST(ShadowDecisionTest, ExecutionBeforeDeliveryAndDecideAfterWithSameDecisionId)
{
    ShadowFixture fixture;
    std::vector<std::string> events;
    OrderedPolicy policy(events);
    const std::string executionId = fixture.execution.executionId;
    std::string decisionIdSeenAtDelivery;

    const std::string decisionId = recordShadowAroundDelivery(
        fixture.recorder, policy, fixture.card, "群背景\n触发句：这是谁", "msg-1",
        fixture.execution,
        [&]()
        {
            events.push_back("deliver");
            const auto execution = fixture.store.findExecution(executionId);
            ASSERT_TRUE(execution.has_value()) << "投递时执行行必须已存在（回执规则）";
            EXPECT_EQ(execution->deviationReason, "shadow");
            decisionIdSeenAtDelivery = execution->decisionId;
            EXPECT_FALSE(fixture.store.findDecision(execution->decisionId).has_value())
                << "决策行在 judge 之后才写";
        });

    EXPECT_EQ(events, (std::vector<std::string>{"deliver", "decide"}));
    ASSERT_FALSE(decisionId.empty());
    EXPECT_EQ(decisionIdSeenAtDelivery, decisionId);
    const auto decision = fixture.store.findDecision(decisionId);
    ASSERT_TRUE(decision.has_value());
    EXPECT_EQ(decision->source, "at_reply");
    EXPECT_EQ(decision->userId, 1477646U);
    EXPECT_EQ(decision->triggerMessageId, "msg-1");
    EXPECT_EQ(decision->policyVersion, "test_v1");
    EXPECT_EQ(decision->stateJson.find("这是谁"), std::string::npos) << "材料原文不落库";
    EXPECT_EQ(fixture.store.findExecution(executionId)->decisionId, decisionId);
}

// judge 失败：按回退写决策（基线计划 + fallback_reason），不留孤儿执行行
TEST(ShadowDecisionTest, JudgeFailureStillWritesFallbackDecision)
{
    ShadowFixture fixture;
    affect::JudgePolicy judge([](const std::string &, const std::string &) { return std::string(); },
                              "judge-model");
    const std::string decisionId = recordShadowAroundDelivery(
        fixture.recorder, judge, fixture.card, "m", "msg-2", fixture.execution, [] {});
    const auto decision = fixture.store.findDecision(decisionId);
    ASSERT_TRUE(decision.has_value());
    EXPECT_EQ(decision->plan.action, "reply");
    EXPECT_EQ(decision->fallbackReason, "judge_empty");
    EXPECT_EQ(decision->policyVersion, "judge_v1");
}

// 投递抛异常：照样补写决策再把异常抛出去
TEST(ShadowDecisionTest, DeliveryExceptionStillWritesDecisionThenRethrows)
{
    ShadowFixture fixture;
    std::vector<std::string> events;
    OrderedPolicy policy(events);
    const std::string executionId = fixture.execution.executionId;
    EXPECT_THROW(recordShadowAroundDelivery(fixture.recorder, policy, fixture.card, "m", "msg-3",
                                            fixture.execution,
                                            [] { throw std::runtime_error("send failed"); }),
                 std::runtime_error);
    const auto execution = fixture.store.findExecution(executionId);
    ASSERT_TRUE(execution.has_value());
    EXPECT_TRUE(fixture.store.findDecision(execution->decisionId).has_value());
    EXPECT_EQ(events, std::vector<std::string>{"decide"});
}

// 记录器关闭：只投递，不调策略
TEST(ShadowDecisionTest, DisabledRecorderOnlyDelivers)
{
    ShadowFixture fixture;
    affect::DecisionRecorder disabled;
    std::vector<std::string> events;
    OrderedPolicy policy(events);
    const std::string decisionId = recordShadowAroundDelivery(
        disabled, policy, fixture.card, "m", "msg-4", fixture.execution,
        [&] { events.push_back("deliver"); });
    EXPECT_TRUE(decisionId.empty());
    EXPECT_EQ(events, std::vector<std::string>{"deliver"});
}
} // namespace
