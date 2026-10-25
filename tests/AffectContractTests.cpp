#include <gtest/gtest.h>

#include "Affect/BehaviorPlan.h"
#include "Affect/DecisionRecord.h"
#include "Affect/DecisionRecorder.h"
#include "Affect/DecisionStore.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-affect-XXXXXX";
        pattern.push_back('\0');
        char *created = mkdtemp(pattern.data());
        if (created != nullptr)
            directory = created;
    }

    ~TemporaryDirectory()
    {
        if (!directory.empty())
            std::filesystem::remove_all(directory);
    }

    const std::string &path() const { return directory; }

private:
    std::string directory;
};
} // namespace

TEST(BehaviorPlanTest, ValidPlanRoundTripsJson)
{
    affect::BehaviorPlan plan;
    plan.action = "group_say";
    plan.speechAct = "acknowledge_then_ask";
    plan.tone = "warm";
    plan.maxCodepoints = 80;
    plan.toolPolicy = "allow_list";
    plan.allowedTools = {"group_say", "recall_context"};

    std::string error;
    ASSERT_TRUE(plan.validate(error)) << error;

    const nlohmann::json json = plan.toJson();
    const std::optional<affect::BehaviorPlan> parsed =
        affect::BehaviorPlan::fromJson(json, error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_EQ(parsed->schemaVersion, 1);
    EXPECT_EQ(parsed->action, "group_say");
    EXPECT_EQ(parsed->speechAct, "acknowledge_then_ask");
    EXPECT_EQ(parsed->tone, "warm");
    EXPECT_EQ(parsed->maxCodepoints, 80);
    EXPECT_EQ(parsed->toolPolicy, "allow_list");
    EXPECT_EQ(parsed->allowedTools, (std::vector<std::string>{"group_say", "recall_context"}));
}

TEST(BehaviorPlanTest, RejectsInvalidSchemaActionAndToolPolicy)
{
    std::string error;
    affect::BehaviorPlan plan;
    plan.action = "reply";

    plan.schemaVersion = 2;
    EXPECT_FALSE(plan.validate(error));
    plan.schemaVersion = 1;

    plan.action = "unknown_action";
    EXPECT_FALSE(plan.validate(error));
    plan.action = "reply";

    plan.toolPolicy = "allow_list";
    plan.allowedTools = {"same", "same"};
    EXPECT_FALSE(plan.validate(error));
    plan.allowedTools.clear();

    plan.toolPolicy = "unknown_policy";
    EXPECT_FALSE(plan.validate(error));
    plan.toolPolicy = "inherit";

    plan.action = "no_reply";
    EXPECT_FALSE(plan.validate(error));

    plan.toolPolicy = "none";
    EXPECT_TRUE(plan.validate(error)) << error;
}

TEST(BehaviorPlanTest, RejectsMalformedJson)
{
    std::string error;
    nlohmann::json json = {{"schema_version", 1}};
    EXPECT_FALSE(affect::BehaviorPlan::fromJson(json, error).has_value());
    EXPECT_FALSE(error.empty());

    json = {{"schema_version", 1}, {"action", "reply"}, {"allowed_tools", "not-an-array"}};
    EXPECT_FALSE(affect::BehaviorPlan::fromJson(json, error).has_value());
    EXPECT_FALSE(error.empty());
}

TEST(DecisionRecordTest, BaselineFactoryKeepsUnifiedDefaults)
{
    const affect::DecisionRecord reply = affect::makeBaselineDecision(
        "at_reply", 8823, 10001, "msg-1", "reply");
    EXPECT_EQ(reply.source, "at_reply");
    EXPECT_EQ(reply.groupId, 8823U);
    EXPECT_EQ(reply.userId, 10001U);
    EXPECT_EQ(reply.triggerMessageId, "msg-1");
    EXPECT_EQ(reply.plan.action, "reply");
    EXPECT_EQ(reply.plan.speechAct, "unspecified");
    EXPECT_EQ(reply.plan.tone, "unspecified");
    EXPECT_EQ(reply.plan.toolPolicy, "inherit");
    EXPECT_EQ(reply.policyVersion, "baseline_v2.4.1");
    EXPECT_EQ(reply.planVersion, "baseline_plan_v1");

    const affect::DecisionRecord silence = affect::makeBaselineDecision(
        "engagement_turn", 8823, 0, "", "no_reply");
    EXPECT_EQ(silence.plan.action, "no_reply");
    EXPECT_EQ(silence.plan.toolPolicy, "none");
}

TEST(DecisionStoreTest, PersistsDecisionExecutionAndFeedbackAcrossReopen)
{
    TemporaryDirectory directory;
    ASSERT_FALSE(directory.path().empty());
    const std::string dbPath = directory.path() + "/affect.db";

    std::string decisionId;
    {
        affect::DecisionStore store(dbPath);
        ASSERT_TRUE(store.isOpen());
        affect::DecisionRecorder recorder(&store, [] { return 1234; });

        affect::DecisionRecord decision = affect::makeBaselineDecision(
            "at_reply", 8823, 10001, "msg-1", "reply");
        decisionId = recorder.recordDecision(decision);
        ASSERT_FALSE(decisionId.empty());
        EXPECT_EQ(store.decisionCount(), 1U);

        affect::ExecutionRecord execution;
        execution.decisionId = decisionId;
        execution.status = "queued";
        execution.actualText = "在呢";
        EXPECT_TRUE(recorder.recordExecution(execution));
        EXPECT_EQ(store.executionCount(decisionId), 1U);

        affect::FeedbackRecord feedback;
        feedback.decisionId = decisionId;
        feedback.source = "explicit_reply";
        feedback.status = "known";
        feedback.confidence = 0.9;
        EXPECT_TRUE(recorder.recordFeedback(feedback));
        EXPECT_EQ(store.feedbackCount(decisionId), 1U);
    }

    // 重开验证持久化，而不是只存在内存里
    affect::DecisionStore reopened(dbPath);
    ASSERT_TRUE(reopened.isOpen());
    const std::optional<affect::DecisionRecord> found = reopened.findDecision(decisionId);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->source, "at_reply");
    EXPECT_EQ(found->groupId, 8823U);
    EXPECT_EQ(found->userId, 10001U);
    EXPECT_EQ(found->createdAt, 1234);
    EXPECT_EQ(found->plan.action, "reply");
    EXPECT_EQ(reopened.executionCount(decisionId), 1U);
    EXPECT_EQ(reopened.feedbackCount(decisionId), 1U);

    const auto recent = reopened.recentDecisions(10);
    ASSERT_EQ(recent.size(), 1U);
    EXPECT_EQ(recent.front().decisionId, decisionId);
}

TEST(DecisionStoreTest, DuplicateIdsAreIdempotent)
{
    TemporaryDirectory directory;
    affect::DecisionStore store(directory.path() + "/affect.db");
    ASSERT_TRUE(store.isOpen());

    affect::DecisionRecord decision = affect::makeBaselineDecision(
        "engagement_turn", 8823, 0, "", "group_say");
    decision.decisionId = "dec-fixed";
    ASSERT_TRUE(store.appendDecision(decision));
    ASSERT_TRUE(store.appendDecision(decision));
    EXPECT_EQ(store.decisionCount(), 1U);

    affect::ExecutionRecord execution;
    execution.executionId = "exe-fixed";
    execution.decisionId = "dec-fixed";
    execution.status = "queued";
    ASSERT_TRUE(store.appendExecution(execution));
    ASSERT_TRUE(store.appendExecution(execution));
    EXPECT_EQ(store.executionCount("dec-fixed"), 1U);

    affect::FeedbackRecord feedback;
    feedback.feedbackId = "fbk-fixed";
    feedback.decisionId = "dec-fixed";
    feedback.source = "explicit_reply";
    feedback.status = "unknown";
    ASSERT_TRUE(store.appendFeedback(feedback));
    ASSERT_TRUE(store.appendFeedback(feedback));
    EXPECT_EQ(store.feedbackCount("dec-fixed"), 1U);

    affect::FeedbackRecord invalid = feedback;
    invalid.feedbackId = "fbk-invalid";
    invalid.confidence = 1.5;
    EXPECT_FALSE(store.appendFeedback(invalid));
    EXPECT_EQ(store.feedbackCount("dec-fixed"), 1U);
}

TEST(DecisionRecorderTest, DisabledWithoutStore)
{
    affect::DecisionRecorder recorder(nullptr);
    EXPECT_FALSE(recorder.enabled());

    affect::DecisionRecord decision = affect::makeBaselineDecision(
        "at_reply", 1, 2, "msg", "reply");
    EXPECT_TRUE(recorder.recordDecision(decision).empty());

    affect::ExecutionRecord execution;
    execution.decisionId = "missing";
    execution.status = "queued";
    EXPECT_FALSE(recorder.recordExecution(execution));
}
