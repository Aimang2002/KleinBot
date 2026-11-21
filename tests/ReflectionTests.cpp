#include <gtest/gtest.h>

#include "Action/ReflectAction.h"
#include "Affect/BehaviorPlan.h"
#include "Affect/DecisionRecord.h"
#include "Affect/DecisionRecorder.h"
#include "Affect/DecisionStore.h"
#include "Affect/KnowledgeLedger.h"
#include "Affect/Reflection.h"
#include "Perception/GroupContextStore.h"
#include "Perception/ReflectionService.h"
#include "utils/LocalDate.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

// 经历层 E3（ADR-030）：回想——材料装配、模型输出的校验与落账、调度、手动触发
namespace
{
constexpr std::uint64_t kGroup = 882926802;
constexpr std::uint64_t kBot = 987654321;
constexpr std::uint64_t kManager = 10001;
constexpr std::uint64_t kAqing = 20002;
constexpr std::uint64_t kLaoyu = 30003;
constexpr std::int64_t kMinute = 60;
constexpr std::int64_t kHour = 3600;

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-reflection-XXXXXX";
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

struct Harness
{
    TemporaryDirectory directory;
    Live<AffectOptions> affect{AffectOptions{true, "judge", false, ""}};
    Live<BotIdentity> identity{BotIdentity{kBot, kManager, "Klein"}};
    // 当天 10:00 起（本地日历日内，当日帽不跨零点）
    std::int64_t now = static_cast<std::int64_t>(localDayStart(1'800'000'000)) + 10 * kHour;
    affect::DecisionStore store{directory.path() + "/conversation.db"};
    affect::DecisionRecorder recorder{store, affect, [this] { return now; }};
    affect::KnowledgeLedger ledger{directory.path() + "/conversation.db", [this] { return now; }};
    GroupContextStore groups{directory.path() + "/conversation.db"};
    std::vector<std::string> systems;
    std::vector<std::string> users;
    std::string reply; // 模型回什么
    bool workerAvailable = true;
    affect::ReflectionService service{store,
                                      recorder,
                                      ledger,
                                      &groups,
                                      affect,
                                      identity,
                                      [this]() -> affect::ReflectionService::Target
                                      {
                                          if (!workerAvailable)
                                              return {};
                                          return {[this](const std::string &system, const std::string &user)
                                                  {
                                                      systems.push_back(system);
                                                      users.push_back(user);
                                                      return reply;
                                                  },
                                                  "fake-reflect"};
                                      },
                                      [this] { return now; }};

    Harness()
    {
        EXPECT_TRUE(recorder.enabled()); // 打开决策库
        EXPECT_TRUE(ledger.open());
        service.setSoulSource([] { return std::string("她话不多，接话先接情绪。"); });
        service.setFactsSource([] { return std::string("她住在上海。"); });
    }

    void say(std::uint64_t userId, const std::string &nickname, const std::string &text,
             std::int64_t at, const std::string &messageId = {})
    {
        GroupMessageRecord record;
        record.groupId = kGroup;
        record.userId = userId;
        record.nickname = nickname;
        record.text = text;
        record.timestamp = at;
        record.messageId = messageId;
        groups.append(record);
    }

    // 她在会话轮里说了一句：决策（计划 v2）+ 执行 + 群缓冲里的自消息；返回决策 id
    std::string speak(const std::string &text, std::int64_t at, const std::string &occasion = "大家在聊加班",
                      const std::string &status = "sent")
    {
        affect::DecisionRecord decision;
        decision.source = "engagement_turn";
        decision.groupId = kGroup;
        decision.createdAt = at;
        decision.plan.schemaVersion = affect::BehaviorPlan::kSchemaVersionV2;
        decision.plan.action = "group_say";
        decision.plan.move = "comfort";
        decision.plan.length = "one";
        decision.plan.occasion = occasion;
        decision.plan.why = "test";
        decision.policyVersion = "judge_v2";
        decision.planVersion = "judge_plan_v2";
        const std::string id = recorder.recordDecision(decision);
        EXPECT_FALSE(id.empty());
        affect::ExecutionRecord execution;
        execution.executionId = affect::DecisionRecorder::newExecutionId();
        execution.decisionId = id;
        execution.createdAt = at;
        execution.status = status;
        execution.actualText = status == "skipped" ? std::string() : text;
        EXPECT_TRUE(recorder.recordExecution(execution));
        if (status != "skipped")
        {
            GroupMessageRecord self;
            self.groupId = kGroup;
            self.userId = kBot;
            self.nickname = "Klein";
            self.text = text;
            self.timestamp = at;
            self.isSelf = true;
            groups.append(self);
        }
        return id;
    }

    std::string inTheMoment(const std::string &decisionId, const std::string &reading, std::int64_t at)
    {
        affect::FeedbackRecord feedback;
        feedback.decisionId = decisionId;
        feedback.createdAt = at;
        feedback.source = "in_the_moment";
        feedback.status = "known";
        feedback.evidenceJson = nlohmann::json{{"reading", reading}, {"observed_in", "dec_x"}}.dump();
        EXPECT_TRUE(recorder.recordFeedback(feedback));
        return feedback.feedbackId;
    }

    std::int64_t teach(const std::string &text)
    {
        affect::KnowledgeChange change;
        change.op = "add";
        change.kind = "habit";
        change.key = "t_test";
        change.text = text;
        change.searchText = text;
        affect::KnowledgeRunContext context;
        context.origin = "teaching";
        context.evidenceExists = [](const std::string &) { return true; };
        const affect::KnowledgeRunResult result = ledger.apply({change}, context);
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(result.written.size(), 1u);
        return result.written.empty() ? 0 : result.written.front();
    }

    // 一次典型的经历：她 10:00 说了一句，之后两人接话，10:05 判定器写了当场察觉；现在是 11:00
    std::string typicalExperience()
    {
        say(kAqing, "阿青", "今天又加班到十点", now - kHour);
        const std::string id = speak("辛苦了，先吃口热的", now - kHour + kMinute);
        say(kLaoyu, "老余", "加班狗抱团取暖才是正经事", now - kHour + 2 * kMinute);
        say(kAqing, "阿青", "热的都凉了", now - kHour + 3 * kMinute);
        inTheMoment(id, "她接了一句体谅，阿青接着说下去了", now - kHour + 5 * kMinute);
        return id;
    }
};

std::string okOutput(const std::string &decisionId)
{
    return nlohmann::json{
        {"readings",
         {{{"decision_id", decisionId},
           {"reading", "我接了句体谅，两个人都接着聊，算接住了"},
           {"polarity", "good"},
           {"confidence", 0.8}}}},
        {"changes",
         {{{"op", "add"},
           {"kind", "habit"},
           {"scope_group", 0},
           {"scope_user", 0},
           {"text", "别人说加班累的时候，我先接一句体谅，不急着出主意"},
           {"search_text", "加班 累 体谅"},
           {"evidence", {"decision:" + decisionId}},
           {"why", "两次都接住了"}}}}}
        .dump();
}
} // namespace

TEST(ReflectionTest, PromptCarriesExperienceAftermathKnowledgeAndPeople)
{
    Harness harness;
    const std::int64_t taught = harness.teach("群里别聊政治");
    const std::string id = harness.typicalExperience();
    harness.say(kManager, "阿芒", "克莱茵今天状态不错", harness.now - 30 * kMinute);
    harness.reply = okOutput(id);

    const affect::ReflectionReport report = harness.service.runNow(kGroup, true);
    ASSERT_TRUE(report.ran);
    EXPECT_EQ(report.experiences, 1u);
    ASSERT_EQ(harness.users.size(), 1u);
    const std::string &prompt = harness.users.front();
    // 人格、事实、此刻、群号、部署者昵称
    EXPECT_NE(prompt.find("她话不多，接话先接情绪。"), std::string::npos);
    EXPECT_NE(prompt.find("她住在上海。"), std::string::npos);
    EXPECT_NE(prompt.find("此刻：现在是"), std::string::npos);
    EXPECT_NE(prompt.find("群号：" + std::to_string(kGroup)), std::string::npos);
    EXPECT_NE(prompt.find("部署者在这个群里的昵称是“阿芒”"), std::string::npos);
    // 她的认识带编号与“教导”标记
    EXPECT_NE(prompt.find("#" + std::to_string(taught) + "｜到处都算｜教导：群里别聊政治"), std::string::npos);
    // 经历：编号、场合、做法、她的话、之后的现场、已有读法
    EXPECT_NE(prompt.find("经历 decision:" + id), std::string::npos);
    EXPECT_NE(prompt.find("当时的场合：大家在聊加班"), std::string::npos);
    EXPECT_NE(prompt.find("她的打算：group_say / comfort"), std::string::npos);
    EXPECT_NE(prompt.find("她说的：辛苦了，先吃口热的"), std::string::npos);
    EXPECT_NE(prompt.find("老余: 加班狗抱团取暖"), std::string::npos);
    EXPECT_NE(prompt.find("已有的读法 feedback:"), std::string::npos);
    EXPECT_NE(prompt.find("她接了一句体谅，阿青接着说下去了"), std::string::npos);
    // 她那句之前的现场不在“之后的现场”里
    EXPECT_EQ(prompt.find("今天又加班到十点"), std::string::npos);
    // 在场的人（不含她自己）
    EXPECT_NE(prompt.find("阿青（QQ " + std::to_string(kAqing) + "）"), std::string::npos);
    EXPECT_NE(prompt.find("老余（QQ " + std::to_string(kLaoyu) + "）"), std::string::npos);
    EXPECT_EQ(prompt.find("Klein（QQ"), std::string::npos);
    // 她没参与的群聊：经历现场之后的那句
    EXPECT_NE(prompt.find("她没参与的一段群聊：\n"), std::string::npos);
    EXPECT_NE(prompt.find("阿芒: 克莱茵今天状态不错"), std::string::npos);
    // system 只讲机制
    EXPECT_NE(harness.systems.front().find("只输出一个 JSON 对象"), std::string::npos);
}

TEST(ReflectionTest, WritesReadingsAsFeedbackAndChangesIntoLedgerThenAdvancesCursor)
{
    Harness harness;
    const std::string id = harness.typicalExperience();
    harness.reply = okOutput(id);

    const affect::ReflectionReport report = harness.service.runNow(kGroup, true);
    EXPECT_TRUE(report.ran);
    EXPECT_EQ(report.readings, 1u);
    EXPECT_EQ(report.written, 1u);
    EXPECT_EQ(report.rejected, 0u);
    EXPECT_EQ(report.modelLabel, "fake-reflect");
    EXPECT_FALSE(report.runId.empty());

    // 读法成了那次决策的反馈（source=self_reflection），evidence 带读法、极性、运行号
    const std::vector<affect::FeedbackRecord> feedback = harness.store.feedbackFor(id);
    ASSERT_EQ(feedback.size(), 2u);
    EXPECT_EQ(feedback.back().source, "self_reflection");
    EXPECT_EQ(feedback.back().status, "known");
    EXPECT_DOUBLE_EQ(feedback.back().confidence, 0.8);
    const nlohmann::json evidence = nlohmann::json::parse(feedback.back().evidenceJson);
    EXPECT_EQ(evidence.value("polarity", ""), "good");
    EXPECT_EQ(evidence.value("run_id", ""), report.runId);
    EXPECT_NE(evidence.value("reading", "").find("接住了"), std::string::npos);

    // 认识进了账本：作者回想、强度 0.6、全局习惯、key 由正文哈希生成
    const std::vector<affect::KnowledgeEntry> entries = harness.ledger.query({});
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().kind, "habit");
    EXPECT_EQ(entries.front().scopeGroup, 0u);
    EXPECT_DOUBLE_EQ(entries.front().strength, affect::KnowledgeRules::kReflectionStrength);
    EXPECT_EQ(entries.front().key, affect::reflectionKey(entries.front().text));
    EXPECT_EQ(harness.ledger.authorOf(entries.front().id), "reflection");
    const std::vector<affect::KnowledgeHistoryRow> history = harness.ledger.history(entries.front().id);
    ASSERT_EQ(history.size(), 1u);
    EXPECT_EQ(history.front().runId, report.runId);
    EXPECT_EQ(history.front().origin, "reflection");

    // 游标前移：再跑一次没有经历，不再调模型
    const affect::ReflectionProgress progress = harness.service.progressOf(kGroup);
    EXPECT_EQ(progress.cursor, harness.now - kHour + kMinute);
    EXPECT_EQ(progress.lastRun, harness.now);
    EXPECT_EQ(progress.runsToday, 0); // 手动不计入当日帽
    const affect::ReflectionReport again = harness.service.runNow(kGroup, true);
    EXPECT_FALSE(again.ran);
    EXPECT_EQ(again.skipped, "没有待回想的经历");
    EXPECT_EQ(harness.users.size(), 1u);
}

TEST(ReflectionTest, DropsInvalidReadingsAndChangesButKeepsTheRest)
{
    Harness harness;
    const std::string id = harness.typicalExperience();
    harness.reply = nlohmann::json{
        {"readings",
         {{{"decision_id", "dec_nope"}, {"reading", "不存在的经历"}, {"polarity", "good"}},
          {{"decision_id", id}, {"reading", "极性不对"}, {"polarity", "great"}},
          {{"decision_id", id}, {"reading", "老余说加班狗抱团取暖才是正经事，我"}, {"polarity", "good"}}, // 照抄原文
          {{"decision_id", id}, {"reading", "这次接住了"}, {"polarity", "good"}, {"confidence", 2.0}}}},
        {"changes",
         {{{"op", "add"}, {"kind", "person"}, {"scope_user", 99999}, {"text", "这个人不在材料里"}},
          {{"op", "add"}, {"kind", "group"}, {"scope_group", 1}, {"text", "别的群的认识"}},
          {{"op", "add"}, {"kind", "person"}, {"scope_user", kLaoyu}, {"text", "老余爱接梗，接他的话不会冷场"}}}}}
        .dump();

    const affect::ReflectionReport report = harness.service.runNow(kGroup, true);
    EXPECT_EQ(report.readings, 1u);
    EXPECT_EQ(report.written, 1u);
    EXPECT_EQ(report.rejected, 5u);
    const std::vector<affect::FeedbackRecord> feedback = harness.store.feedbackFor(id);
    ASSERT_EQ(feedback.size(), 2u);
    EXPECT_DOUBLE_EQ(feedback.back().confidence, 1.0); // 夹到 0–1
    const std::vector<affect::KnowledgeEntry> entries = harness.ledger.query({});
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().kind, "person");
    EXPECT_EQ(entries.front().scopeUser, kLaoyu);
    // 没写依据的改动按这次的经历算
    const std::vector<affect::KnowledgeHistoryRow> history = harness.ledger.history(entries.front().id);
    ASSERT_EQ(history.size(), 1u);
    const nlohmann::json evidence = nlohmann::json::parse(history.front().evidenceJson);
    ASSERT_TRUE(evidence.contains("ids"));
    EXPECT_EQ(evidence["ids"].size(), 1u);
    EXPECT_EQ(evidence["ids"][0], "decision:" + id);
    EXPECT_EQ(evidence.value("model", ""), "fake-reflect");
}

TEST(ReflectionTest, TeachingIsNotTouchedByReflection)
{
    Harness harness;
    const std::int64_t taught = harness.teach("群里别聊政治");
    const std::string id = harness.typicalExperience();
    harness.reply = nlohmann::json{
        {"readings", nlohmann::json::array()},
        {"changes",
         {{{"op", "withdraw"}, {"id", taught}, {"evidence", {"decision:" + id}}},
          {{"op", "modify"}, {"id", taught}, {"text", "群里可以聊政治"}, {"evidence", {"decision:" + id}}}}}}
        .dump();
    const affect::ReflectionReport report = harness.service.runNow(kGroup, true);
    EXPECT_EQ(report.written, 0u);
    EXPECT_EQ(report.rejected, 2u);
    const std::optional<affect::KnowledgeEntry> entry = harness.ledger.find(taught);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->status, "active");
    EXPECT_EQ(entry->text, "群里别聊政治");
}

TEST(ReflectionTest, ModelSilenceLeavesCursorAloneAndGarbageAdvancesIt)
{
    Harness harness;
    harness.typicalExperience();
    harness.reply.clear();
    const affect::ReflectionReport silent = harness.service.runNow(kGroup, true);
    EXPECT_TRUE(silent.ran);
    EXPECT_EQ(silent.skipped, "模型没有返回");
    EXPECT_EQ(harness.service.progressOf(kGroup).cursor, 0);

    harness.reply = "这不是 JSON";
    const affect::ReflectionReport garbage = harness.service.runNow(kGroup, true);
    EXPECT_TRUE(garbage.ran);
    EXPECT_TRUE(garbage.skipped.empty());
    EXPECT_EQ(garbage.readings, 0u);
    EXPECT_EQ(garbage.written, 0u);
    EXPECT_EQ(harness.service.progressOf(kGroup).cursor, harness.now - kHour + kMinute);
    EXPECT_EQ(harness.store.feedbackFor(harness.store.recentDecisions(1).front().decisionId).size(), 1u);
}

// 沉默也是经历（2026-11-21 部署者定规：她的每个动作都该回想）；影子期标出判定器的打算没有生效
TEST(ReflectionTest, SilenceAndLeavingAreExperiencesToo)
{
    Harness harness;
    const std::string silent = harness.speak("", harness.now - kHour, "大家在聊加班", "skipped");
    harness.say(kAqing, "阿青", "没人理我吗", harness.now - kHour + kMinute);
    harness.reply = okOutput(silent);
    const affect::ReflectionReport report = harness.service.runNow(kGroup, true);
    EXPECT_TRUE(report.ran);
    EXPECT_EQ(report.experiences, 1u);
    ASSERT_EQ(harness.users.size(), 1u);
    EXPECT_NE(harness.users.front().find("她的打算：group_say / comfort"), std::string::npos);
    EXPECT_NE(harness.users.front().find("她做的：没开口"), std::string::npos);
    EXPECT_EQ(harness.users.front().find("她说的："), std::string::npos);
    EXPECT_NE(harness.users.front().find("阿青: 没人理我吗"), std::string::npos);
    EXPECT_EQ(report.readings, 1u);
    EXPECT_EQ(harness.service.progressOf(kGroup).cursor, harness.now - kHour);
    EXPECT_TRUE(harness.service.dueGroups(harness.now).empty());

    // 影子期：执行记录的 deviation 是 shadow，prompt 里标出“没有生效”
    const std::string shadow = harness.speak("基线说的", harness.now - 30 * kMinute, "大家在聊加班");
    affect::ExecutionRecord deviated;
    deviated.executionId = affect::DecisionRecorder::newExecutionId();
    deviated.decisionId = shadow;
    deviated.createdAt = harness.now - 30 * kMinute;
    deviated.status = "sent";
    deviated.actualText = "基线说的";
    deviated.deviationReason = "shadow";
    EXPECT_TRUE(harness.recorder.recordExecution(deviated));
    harness.reply = okOutput(shadow);
    EXPECT_TRUE(harness.service.runNow(kGroup, true).ran);
    ASSERT_EQ(harness.users.size(), 2u);
    EXPECT_NE(harness.users.back().find("（影子期，这个打算没有生效）"), std::string::npos);
    EXPECT_NE(harness.users.back().find("她说的：基线说的"), std::string::npos);
}

TEST(ReflectionTest, AutomaticScheduleHonoursAftermathWindowIntervalAndDailyCap)
{
    Harness harness;
    // 说完才 10 分钟：后续窗口没关，不到期
    const std::string fresh = harness.speak("刚说的", harness.now - 10 * kMinute);
    EXPECT_TRUE(harness.service.dueGroups(harness.now).empty());
    // 过了 20 分钟：到期（从没回想过，间隔算满足）
    harness.now += 15 * kMinute;
    EXPECT_EQ(harness.service.dueGroups(harness.now), std::vector<std::uint64_t>{kGroup});

    harness.reply = okOutput(fresh);
    EXPECT_TRUE(harness.service.runNow(kGroup, false).ran);
    EXPECT_EQ(harness.service.progressOf(kGroup).runsToday, 1);

    // 新的经历，但距上次不到 6 小时：不到期
    harness.now += kHour;
    harness.speak("又说了一句", harness.now - 30 * kMinute);
    EXPECT_TRUE(harness.service.dueGroups(harness.now).empty());
    harness.now += 5 * kHour;
    EXPECT_EQ(harness.service.dueGroups(harness.now), std::vector<std::uint64_t>{kGroup});

    // 当日帽：今天已自动回想 4 次就不再到期（手动不受限）
    affect::ReflectionProgress progress = harness.service.progressOf(kGroup);
    progress.runsToday = affect::ReflectionRules::kDailyCap;
    progress.day = static_cast<std::int64_t>(localDayStart(static_cast<std::time_t>(harness.now)));
    harness.ledger.writeMeta("affect.reflection." + std::to_string(kGroup),
                             nlohmann::json{{"cursor", progress.cursor},
                                            {"last_run", progress.lastRun},
                                            {"day", progress.day},
                                            {"runs_today", progress.runsToday}}
                                 .dump());
    EXPECT_TRUE(harness.service.dueGroups(harness.now).empty());
    EXPECT_EQ(harness.service.requestNow(kGroup), 1u);
    // 第二天零点过后帽子清零
    harness.now = static_cast<std::int64_t>(localDayStart(static_cast<std::time_t>(harness.now))) + 24 * kHour +
                  kMinute;
    EXPECT_EQ(harness.service.dueGroups(harness.now), std::vector<std::uint64_t>{kGroup});
}

TEST(ReflectionTest, EarlyTriggerWhenBufferPilesUpAfterTheExperience)
{
    Harness harness;
    const std::string first = harness.speak("第一句", harness.now - 2 * kHour);
    harness.reply = okOutput(first);
    EXPECT_TRUE(harness.service.runNow(kGroup, false).ran);
    // 刚回想过；新的经历之后群里涌进 200 条：原文快过期了，提前到期
    const std::int64_t spokenAt = harness.now - kHour;
    harness.speak("第二句", spokenAt);
    for (std::size_t index = 0; index < affect::ReflectionRules::kEarlyTriggerMessages - 1; ++index)
        harness.say(kAqing, "阿青", "刷屏 " + std::to_string(index), spokenAt + 1 + static_cast<std::int64_t>(index));
    EXPECT_TRUE(harness.service.dueGroups(harness.now).empty());
    harness.say(kAqing, "阿青", "再来一条", spokenAt + 300);
    EXPECT_EQ(harness.service.dueGroups(harness.now), std::vector<std::uint64_t>{kGroup});
}

TEST(ReflectionTest, AffectOffOrNoWorkerDoesNothing)
{
    Harness harness;
    harness.typicalExperience();
    harness.affect.set(AffectOptions{false, "judge", false, ""});
    EXPECT_EQ(harness.service.runNow(kGroup, true).skipped, "affect 关着");
    EXPECT_TRUE(harness.service.dueGroups(harness.now).empty());
    EXPECT_EQ(harness.service.requestNow(0), 0u);
    harness.affect.set(AffectOptions{true, "judge", false, ""});
    harness.workerAvailable = false;
    const affect::ReflectionReport report = harness.service.runNow(kGroup, true);
    EXPECT_EQ(report.skipped, "模型未接入");
    EXPECT_EQ(harness.service.progressOf(kGroup).cursor, 0);
}

TEST(ReflectionTest, ManualRequestRunsOnTheWorkerThread)
{
    Harness harness;
    const std::string id = harness.typicalExperience();
    harness.reply = okOutput(id);
    harness.service.start();
    harness.service.pump(harness.now); // 从没回想过：自动也到期，入队一次
    EXPECT_EQ(harness.service.requestNow(kGroup), 0u); // 已排队，不重复
    for (int tries = 0; tries < 200 && harness.ledger.query({}).empty(); ++tries)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(harness.ledger.query({}).size(), 1u);
    harness.service.shutdown();
    EXPECT_EQ(harness.service.requestNow(kGroup), 0u); // 停了以后不再入队
    EXPECT_EQ(harness.users.size(), 1u);
}

TEST(ReflectionTest, ReflectNowToolSchedulesOnlyInManagerPrivateAndReportsCount)
{
    Harness harness;
    ReflectAction action(harness.affect);
    EXPECT_FALSE(action.available()); // 未 bind
    action.bind(&harness.service);
    EXPECT_TRUE(action.available());
    EXPECT_TRUE(action.managerPrivateOnly());
    EXPECT_TRUE(action.descriptor().requires_admin);
    EXPECT_EQ(action.descriptor().name, "reflect_now");

    ActionContext context;
    context.user_id = kManager;
    ActionResult nothing = action.execute(nlohmann::json::object(), context);
    EXPECT_NE(nothing.content.find("没有待回想的经历"), std::string::npos);
    EXPECT_FALSE(nothing.terminal);

    harness.typicalExperience();
    ActionResult scheduled = action.execute(nlohmann::json{{"group", kGroup}}, context);
    EXPECT_NE(scheduled.content.find("安排了 1 个群"), std::string::npos);

    harness.affect.set(AffectOptions{false, "judge", false, ""});
    EXPECT_FALSE(action.available());
    EXPECT_EQ(action.execute(nlohmann::json::object(), context).content, "功能未开启");
}

TEST(ReflectionParseTest, ToleratesFencesAndRejectsGarbage)
{
    std::string error;
    const std::optional<affect::ReflectionOutput> fenced = affect::parseReflection(
        "```json\n{\"readings\":[{\"decision_id\":\"d1\",\"reading\":\"r\",\"polarity\":\"bad\",\"confidence\":0.3}],"
        "\"changes\":[{\"op\":\"strengthen\",\"id\":\"7\",\"evidence\":[\"feedback:f1\"]}]}\n```",
        error);
    ASSERT_TRUE(fenced.has_value());
    ASSERT_EQ(fenced->readings.size(), 1u);
    EXPECT_EQ(fenced->readings.front().polarity, "bad");
    ASSERT_EQ(fenced->changes.size(), 1u);
    EXPECT_EQ(fenced->changes.front().targetId, 7);
    EXPECT_EQ(fenced->changes.front().op, "strengthen");

    EXPECT_FALSE(affect::parseReflection("好的，我想过了", error).has_value());
    EXPECT_FALSE(affect::parseReflection("{\"foo\":1}", error).has_value());
    EXPECT_EQ(error, "no_fields");

    // 超过上限的多余部分丢弃
    nlohmann::json many = {{"readings", nlohmann::json::array()}, {"changes", nlohmann::json::array()}};
    for (int index = 0; index < 20; ++index)
    {
        many["readings"].push_back({{"decision_id", "d"}, {"reading", "r"}, {"polarity", "good"}});
        many["changes"].push_back({{"op", "add"}, {"kind", "habit"}, {"text", "t" + std::to_string(index)}});
    }
    const std::optional<affect::ReflectionOutput> capped = affect::parseReflection(many.dump(), error);
    ASSERT_TRUE(capped.has_value());
    EXPECT_EQ(capped->readings.size(), affect::ReflectionRules::kMaxReadings);
    EXPECT_EQ(capped->changes.size(), affect::KnowledgeRules::kMaxChangesPerRun);
}

TEST(ReflectionParseTest, ReadingCheckGuardsIdPolarityLengthAndQuoting)
{
    std::string reason;
    const std::vector<std::string> ids = {"d1"};
    const std::vector<std::string> material = {"老余说今天加班狗抱团取暖吧"};
    affect::ReflectionReading reading{"d1", "我接住了", "good", 0.5};
    EXPECT_TRUE(affect::checkReading(reading, ids, material, reason));
    reading.decisionId = "d2";
    EXPECT_FALSE(affect::checkReading(reading, ids, material, reason));
    reading.decisionId = "d1";
    reading.polarity = "meh";
    EXPECT_FALSE(affect::checkReading(reading, ids, material, reason));
    reading.polarity = "unclear";
    reading.reading = "加班狗抱团取暖吧，我";
    EXPECT_FALSE(affect::checkReading(reading, ids, material, reason));
    EXPECT_EQ(reason, "照抄了群聊原文");
    reading.reading = std::string(121, 'a'); // 121 个 ASCII 字符 > 120 码点
    EXPECT_FALSE(affect::checkReading(reading, ids, material, reason));
    reading.reading = "一行\n两行";
    EXPECT_FALSE(affect::checkReading(reading, ids, material, reason));
}
