#include <gtest/gtest.h>

#include "Action/RecordFeedbackAction.h"
#include "Affect/DecisionRecord.h"
#include "Affect/DecisionRecorder.h"
#include "Affect/DecisionStore.h"
#include "Command/CommandRegistry.h"
#include "Command/FeedbackCommand.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-feedback-XXXXXX";
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

// 种一条决策 + 一条已回执的执行，返回 execution_id
std::string seed(affect::DecisionRecorder &recorder, affect::DecisionStore &store,
                 const std::string &source, std::uint64_t groupId, std::uint64_t userId,
                 const std::string &messageId, std::int64_t at)
{
    affect::DecisionRecord decision = affect::makeBaselineDecision(source, groupId, userId, "", "reply");
    decision.createdAt = at;
    const std::string decisionId = recorder.recordDecision(decision);
    affect::ExecutionRecord execution;
    execution.executionId = affect::DecisionRecorder::newExecutionId();
    execution.decisionId = decisionId;
    execution.createdAt = at;
    execution.status = "queued";
    execution.actualText = "回复";
    recorder.recordExecution(execution);
    if (!messageId.empty())
        store.updateExecutionReceipt(execution.executionId, "sent", messageId);
    return execution.executionId;
}

class RecordingAction : public Action
{
public:
    const ActionDescriptor &descriptor() const override
    {
        static const ActionDescriptor value{"record", "record", nlohmann::json::object(), false, true};
        return value;
    }
    ActionResult execute(const nlohmann::json &arguments, const ActionContext &context) override
    {
        lastArguments = arguments;
        lastContext = context;
        return {"ok", {}, {}, true};
    }
    nlohmann::json lastArguments;
    ActionContext lastContext;
};

std::string text(const CommandResult &result)
{
    return std::get<TextMessage>(result.payload).content;
}
} // namespace

// ---------- 命令解析 ----------

TEST(FeedbackCommandTest, ParsesLabelsAndNotesWithEitherColon)
{
    std::string label, note;
    EXPECT_TRUE(FeedbackCommand::parse("#好", label, note));
    EXPECT_EQ(label, "good");
    EXPECT_TRUE(note.empty());
    EXPECT_TRUE(FeedbackCommand::parse("  #坏  ", label, note));
    EXPECT_EQ(label, "bad");
    EXPECT_TRUE(FeedbackCommand::parse("#坏:太正式", label, note));
    EXPECT_EQ(label, "bad");
    EXPECT_EQ(note, "太正式");
    EXPECT_TRUE(FeedbackCommand::parse("#好： 接得自然 ", label, note));
    EXPECT_EQ(label, "good");
    EXPECT_EQ(note, "接得自然");
    // 普通聊天不是命令；旧拼写 #差 不再是命令
    EXPECT_FALSE(FeedbackCommand::parse("#好吃", label, note));
    EXPECT_FALSE(FeedbackCommand::parse("好", label, note));
    EXPECT_FALSE(FeedbackCommand::parse("#坏人", label, note));
    EXPECT_FALSE(FeedbackCommand::parse("#差", label, note));
    EXPECT_FALSE(FeedbackCommand::parse("", label, note));
}

TEST(FeedbackCommandTest, MapsReplyIdGroupAndNoteIntoActionArguments)
{
    RecordingAction action;
    FeedbackCommand command(action);
    EXPECT_FALSE(command.requiresAdmin()); // 所有人可用
    EXPECT_TRUE(command.canHandle("#好"));
    EXPECT_FALSE(command.canHandle("#帮助"));

    InboundMessage data;
    data.user_id = 7;
    data.group_id = 88;
    data.message_type = "group";
    data.message_id = 5;
    data.plain_text = "#坏:太正式";
    data.reply_to_message_id = 1131242793;
    CommandContext ctx{data.user_id, data.group_id, data.message_type, data};
    EXPECT_EQ(text(command.execute(ctx)), "ok");
    EXPECT_EQ(action.lastArguments["label"], "bad");
    EXPECT_EQ(action.lastArguments["note"], "太正式");
    EXPECT_EQ(action.lastArguments["reply_to_message_id"], "1131242793");
    EXPECT_EQ(action.lastArguments["group_id"], 88);
    EXPECT_EQ(action.lastContext.user_id, 7U);
    EXPECT_EQ(action.lastContext.user_message_id, 5);

    // 字符串形态消息 ID 优先；私聊 group_id=0；无引用为空串
    data.message_type = "private";
    data.group_id = 0;
    data.plain_text = "#好";
    data.reply_to_message_id_raw = "MsajdvLqdzYxMQA=";
    CommandContext privateCtx{data.user_id, data.group_id, data.message_type, data};
    command.execute(privateCtx);
    EXPECT_EQ(action.lastArguments["reply_to_message_id"], "MsajdvLqdzYxMQA=");
    EXPECT_EQ(action.lastArguments["group_id"], 0);
    data.reply_to_message_id = 0;
    data.reply_to_message_id_raw.clear();
    command.execute(privateCtx);
    EXPECT_EQ(action.lastArguments["reply_to_message_id"], "");
}

TEST(FeedbackCommandTest, RegistryLetsAnyUserGiveFeedback)
{
    RecordingAction action;
    CommandRegistry registry(10001);
    registry.registryCommand(std::make_unique<FeedbackCommand>(action));
    InboundMessage data;
    data.user_id = 2; // 非管理员
    data.message_type = "private";
    data.plain_text = "#好";
    CommandContext ctx{data.user_id, 0, data.message_type, data};
    const auto accepted = registry.execute("#好", ctx);
    ASSERT_TRUE(accepted.has_value());
    EXPECT_EQ(text(*accepted), "ok");
    EXPECT_EQ(action.lastArguments["label"], "good");
    EXPECT_EQ(action.lastContext.user_id, 2U);
    EXPECT_FALSE(registry.execute("随便聊聊", ctx).has_value());
}

// ---------- Action：定位与落库 ----------

TEST(RecordFeedbackActionTest, MatchesByReplyMessageIdAndWritesEvidence)
{
    TemporaryDirectory directory;
    affect::DecisionStore store(directory.path() + "/affect.db");
    ASSERT_TRUE(store.isOpen());
    affect::DecisionRecorder recorder(&store, [] { return 2000; });
    const std::string older = seed(recorder, store, "at_reply", 88, 1, "111", 1500);
    const std::string newer = seed(recorder, store, "at_reply", 88, 1, "222", 1900);
    std::int64_t now = 2000;
    RecordFeedbackAction action(&store, &recorder, 10001, [&now] { return now; });

    const ActionResult result = action.execute(
        {{"label", "good"}, {"note", "接得自然"}, {"reply_to_message_id", "111"}, {"group_id", 88}},
        {10001, 0, "#好:接得自然"});
    EXPECT_NE(result.content.find("记下了：好（接得自然）"), std::string::npos);
    EXPECT_EQ(result.content.find("按最近一次回复匹配"), std::string::npos);

    const auto olderExecution = store.findExecution(older);
    ASSERT_TRUE(olderExecution.has_value());
    EXPECT_EQ(store.feedbackCount(olderExecution->decisionId), 1U);
    EXPECT_EQ(store.feedbackCount(store.findExecution(newer)->decisionId), 0U);
}

TEST(RecordFeedbackActionTest, FallsBackToLatestInWindowPerGroupOrPrivateUser)
{
    TemporaryDirectory directory;
    affect::DecisionStore store(directory.path() + "/affect.db");
    ASSERT_TRUE(store.isOpen());
    affect::DecisionRecorder recorder(&store, [] { return 3000; });
    const std::string groupOld = seed(recorder, store, "at_reply", 88, 1, "", 2000);
    const std::string groupNew = seed(recorder, store, "engagement_turn", 88, 0, "", 2900);
    const std::string otherGroup = seed(recorder, store, "at_reply", 99, 1, "", 2950);
    const std::string privateA = seed(recorder, store, "private_reply", 0, 5, "", 2960);
    const std::string privateB = seed(recorder, store, "private_reply", 0, 6, "", 2970);
    std::int64_t now = 3000;
    RecordFeedbackAction action(&store, &recorder, 10001, [&now] { return now; });

    // 群：没引用 → 该群最近一次（跨 @ 回复与主动轮），不是别的群
    ActionResult result = action.execute({{"label", "bad"}, {"group_id", 88}}, {10001, 0, "#坏"});
    EXPECT_NE(result.content.find("未引用消息，按最近一次回复匹配"), std::string::npos);
    EXPECT_EQ(store.feedbackCount(store.findExecution(groupNew)->decisionId), 1U);
    EXPECT_EQ(store.feedbackCount(store.findExecution(groupOld)->decisionId), 0U);
    EXPECT_EQ(store.feedbackCount(store.findExecution(otherGroup)->decisionId), 0U);

    // 引用了消息但没匹配到记录（分段第二条）→ 同样兜底，提示语不同
    result = action.execute({{"label", "good"}, {"reply_to_message_id", "no-such"}, {"group_id", 88}},
                            {10001, 0, "#好"});
    EXPECT_NE(result.content.find("引用的消息未匹配到记录"), std::string::npos);
    EXPECT_EQ(store.feedbackCount(store.findExecution(groupNew)->decisionId), 2U);

    // 私聊：按本人匹配，不串到别人的私聊
    result = action.execute({{"label", "good"}, {"group_id", 0}}, {5, 0, "#好"});
    EXPECT_EQ(store.feedbackCount(store.findExecution(privateA)->decisionId), 1U);
    EXPECT_EQ(store.feedbackCount(store.findExecution(privateB)->decisionId), 0U);

    // 超出 10 分钟窗口 → 明确拒绝，不乱贴
    now = 2900 + RecordFeedbackAction::kFallbackWindowSeconds + 1;
    result = action.execute({{"label", "bad"}, {"group_id", 88}}, {10001, 0, "#坏"});
    EXPECT_NE(result.content.find("没有可标注的回复"), std::string::npos);
    EXPECT_EQ(store.feedbackCount(store.findExecution(groupNew)->decisionId), 2U);
}

TEST(RecordFeedbackActionTest, SplitsSourceAndConfidenceByAdminVersusUser)
{
    TemporaryDirectory directory;
    affect::DecisionStore store(directory.path() + "/affect.db");
    ASSERT_TRUE(store.isOpen());
    affect::DecisionRecorder recorder(&store, [] { return 500; });
    const std::string executionId = seed(recorder, store, "at_reply", 88, 1, "321", 400);
    const std::string decisionId = store.findExecution(executionId)->decisionId;
    RecordFeedbackAction action(&store, &recorder, 10001, [] { return 500; });

    // 管理员（bot.manager_id）与网友各标一次同一条回复
    action.execute({{"label", "good"}, {"reply_to_message_id", "321"}, {"group_id", 88}},
                   {10001, 0, "#好"});
    action.execute({{"label", "bad"}, {"note", "太正式"}, {"reply_to_message_id", "321"}, {"group_id", 88}},
                   {20002, 0, "#坏:太正式"});

    const auto rows = store.feedbackFor(decisionId);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].source, "manual_admin");
    EXPECT_DOUBLE_EQ(rows[0].confidence, RecordFeedbackAction::kAdminConfidence);
    EXPECT_EQ(nlohmann::json::parse(rows[0].evidenceJson)["rater"], "admin");
    EXPECT_EQ(nlohmann::json::parse(rows[0].evidenceJson)["by"], 10001);
    EXPECT_EQ(rows[1].source, "manual_user");
    EXPECT_DOUBLE_EQ(rows[1].confidence, RecordFeedbackAction::kUserConfidence);
    EXPECT_EQ(nlohmann::json::parse(rows[1].evidenceJson)["rater"], "user");
    EXPECT_EQ(nlohmann::json::parse(rows[1].evidenceJson)["label"], "bad");
    EXPECT_EQ(nlohmann::json::parse(rows[1].evidenceJson)["note"], "太正式");
    for (const auto &row : rows)
        EXPECT_EQ(row.status, "known");

    // 未配置管理员（manager_id=0）时所有人都是网友
    RecordFeedbackAction noManager(&store, &recorder, 0, [] { return 500; });
    noManager.execute({{"label", "good"}, {"reply_to_message_id", "321"}, {"group_id", 88}},
                      {10001, 0, "#好"});
    EXPECT_EQ(store.feedbackFor(decisionId).back().source, "manual_user");
}

TEST(RecordFeedbackActionTest, SkippedExecutionsAreNotFeedbackTargets)
{
    TemporaryDirectory directory;
    affect::DecisionStore store(directory.path() + "/affect.db");
    ASSERT_TRUE(store.isOpen());
    affect::DecisionRecorder recorder(&store, [] { return 100; });
    affect::DecisionRecord decision = affect::makeBaselineDecision("engagement_turn", 88, 0, "", "no_reply");
    const std::string decisionId = recorder.recordDecision(decision);
    affect::ExecutionRecord execution;
    execution.decisionId = decisionId;
    execution.status = "skipped";
    recorder.recordExecution(execution);
    RecordFeedbackAction action(&store, &recorder, 10001, [] { return 100; });
    const ActionResult result = action.execute({{"label", "bad"}, {"group_id", 88}}, {10001, 0, "#坏"});
    EXPECT_NE(result.content.find("没有可标注的回复"), std::string::npos);
    EXPECT_EQ(store.feedbackCount(decisionId), 0U);
}

TEST(RecordFeedbackActionTest, RejectsBadLabelAndReportsDisabledRecorder)
{
    TemporaryDirectory directory;
    affect::DecisionStore store(directory.path() + "/affect.db");
    affect::DecisionRecorder recorder(&store);
    RecordFeedbackAction action(&store, &recorder, 10001);
    EXPECT_NE(action.execute({{"label", "meh"}}, {1, 0, ""}).content.find("label 必须是"),
              std::string::npos);

    RecordFeedbackAction disabled(nullptr, nullptr, 10001);
    EXPECT_NE(disabled.execute({{"label", "good"}}, {1, 0, ""}).content.find("未启用"),
              std::string::npos);
    affect::DecisionRecorder noStore(nullptr);
    RecordFeedbackAction disabledRecorder(&store, &noStore, 10001);
    EXPECT_NE(disabledRecorder.execute({{"label", "good"}}, {1, 0, ""}).content.find("未启用"),
              std::string::npos);

    EXPECT_EQ(action.descriptor().name, "record_feedback");
    EXPECT_FALSE(action.descriptor().expose_as_tool);
    EXPECT_FALSE(action.descriptor().requires_admin); // 所有人可用
}
