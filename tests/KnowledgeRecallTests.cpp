#include <gtest/gtest.h>

#include "Affect/DecisionRecord.h"
#include "Affect/JudgePolicy.h"
#include "Affect/KnowledgeLedger.h"
#include "Affect/KnowledgeRecall.h"
#include "Affect/SituationCard.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// 取用（经历层，ADR-029）：选择规则、拼法、文本稳定性、开关与记录
namespace
{
constexpr std::int64_t kDay = 86400;
constexpr std::uint64_t kGroup = 882926802;
constexpr std::uint64_t kOtherGroup = 123456789;
constexpr std::uint64_t kAqing = 20002;
constexpr std::uint64_t kLaoyu = 30003;

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-recall-XXXXXX";
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

affect::RecalledKnowledge item(std::int64_t id, const std::string &kind, const std::string &text,
                               double effective, bool teaching = false, std::uint64_t scopeGroup = 0,
                               std::uint64_t scopeUser = 0, const std::string &status = "active")
{
    affect::RecalledKnowledge value;
    value.entry.id = id;
    value.entry.kind = kind;
    value.entry.text = text;
    value.entry.scopeGroup = scopeGroup;
    value.entry.scopeUser = scopeUser;
    value.entry.status = status;
    value.entry.strength = effective;
    value.effective = effective;
    value.teaching = teaching;
    return value;
}

std::vector<std::int64_t> idsOf(const std::vector<affect::KnowledgeEntry> &entries)
{
    std::vector<std::int64_t> ids;
    for (const affect::KnowledgeEntry &entry : entries)
        ids.push_back(entry.id);
    return ids;
}

affect::KnowledgeChange change(const std::string &op, const std::string &kind, const std::string &key,
                               const std::string &text, std::uint64_t scopeGroup = 0,
                               std::uint64_t scopeUser = 0)
{
    affect::KnowledgeChange value;
    value.op = op;
    value.kind = kind;
    value.key = key;
    value.text = text;
    value.searchText = text;
    value.scopeGroup = scopeGroup;
    value.scopeUser = scopeUser;
    value.evidence = {"decision:dec_1"};
    return value;
}

affect::KnowledgeRunContext runOf(const std::string &origin, std::uint64_t groupId)
{
    affect::KnowledgeRunContext context;
    context.origin = origin;
    context.groupId = groupId;
    context.presentUsers = {kAqing};
    context.evidenceExists = [](const std::string &) { return true; };
    return context;
}

struct LedgerHarness
{
    TemporaryDirectory directory;
    std::int64_t now = 1763600000;
    affect::KnowledgeLedger ledger{directory.path() + "/affect.db", [this] { return now; }};
    std::string dbPath() const { return directory.path() + "/affect.db"; }
};
} // namespace

// 教导永远优先（哪怕比回想写的弱），其余按有效强度降序截断；最终按编号升序输出
TEST(KnowledgeRecallTest, TeachingFirstThenStrongestAndOutputSortedById)
{
    const std::vector<affect::RecalledKnowledge> pool{
        item(9, "habit", "回想强", 0.9),
        item(2, "habit", "回想中", 0.6),
        item(5, "habit", "教导甲", 0.3, true),
        item(7, "habit", "回想弱", 0.2),
        item(1, "habit", "教导乙", 0.5, true),
        item(4, "habit", "回想次强", 0.8),
    };
    const affect::KnowledgeScope scope{kGroup, {}};
    EXPECT_EQ(idsOf(affect::selectKnowledge(pool, scope, 3)), (std::vector<std::int64_t>{1, 5, 9}))
        << "两条教导先占位，剩一个名额给最强的回想；输出按编号";
    EXPECT_EQ(idsOf(affect::selectKnowledge(pool, scope, 4)), (std::vector<std::int64_t>{1, 4, 5, 9}));
    EXPECT_EQ(idsOf(affect::selectKnowledge(pool, scope, 1)), (std::vector<std::int64_t>{1}))
        << "名额比教导少时，教导之间也按强度";
    EXPECT_EQ(idsOf(affect::selectKnowledge(pool, scope, 10)),
              (std::vector<std::int64_t>{1, 2, 4, 5, 7, 9}));
    EXPECT_TRUE(affect::selectKnowledge(pool, scope, 0).empty());
    EXPECT_EQ(affect::kJudgeKnowledgeMax, 10U);
    EXPECT_EQ(affect::kReplyKnowledgeMax, 6U);
}

// 范围：全局的 + 这个群的 + 现场的人的（person 的 scope_group 为 0 或本群）；别的群的、不在场的人的、
// 不是 active 的、有效强度低于淡出阈值的都不取
TEST(KnowledgeRecallTest, ScopeKeepsGlobalThisGroupAndPresentPeopleOnly)
{
    const std::vector<affect::RecalledKnowledge> pool{
        item(1, "habit", "全局习惯", 0.6),
        item(2, "habit", "本群习惯", 0.6, false, kGroup),
        item(3, "group", "本群认识", 0.6, false, kGroup),
        item(4, "person", "阿青（本群）", 0.6, false, kGroup, kAqing),
        item(5, "person", "老余（全局）", 0.6, false, 0, kLaoyu),
        item(6, "habit", "别的群习惯", 0.6, false, kOtherGroup),
        item(7, "group", "别的群认识", 0.6, false, kOtherGroup),
        item(8, "person", "阿青（别的群）", 0.6, false, kOtherGroup, kAqing),
        item(9, "habit", "淡了的", 0.6, false, 0, 0, "faded"),
        item(10, "habit", "撤回的", 0.6, false, 0, 0, "withdrawn"),
        item(11, "habit", "低于阈值", affect::KnowledgeRules::kFadeThreshold - 0.01),
        item(12, "habit", "正好阈值", affect::KnowledgeRules::kFadeThreshold),
        item(13, "group", "全局的群认识不算", 0.6, false, 0),
    };
    EXPECT_EQ(idsOf(affect::selectKnowledge(pool, {kGroup, {kAqing}}, 20)),
              (std::vector<std::int64_t>{1, 2, 3, 4, 12}))
        << "老余不在场；别的群、淡了、撤回、低于阈值的都不取";
    EXPECT_EQ(idsOf(affect::selectKnowledge(pool, {kGroup, {kAqing, kLaoyu}}, 20)),
              (std::vector<std::int64_t>{1, 2, 3, 4, 5, 12}));
    EXPECT_EQ(idsOf(affect::selectKnowledge(pool, {kOtherGroup, {}}, 20)),
              (std::vector<std::int64_t>{1, 6, 7, 12}))
        << "换个群、没人在场：只有全局的与那个群的";
}

// 拼法：标题行 + 一行一条原文，不带编号、强度、来源；没有条目时为空串（不加空标题）
TEST(KnowledgeRecallTest, SectionIsTitleThenOneLinePerEntry)
{
    EXPECT_EQ(affect::knowledgeSection({}), "");
    const std::vector<affect::RecalledKnowledge> pool{item(3, "habit", "别人倒苦水时我只接一句", 0.6),
                                                      item(1, "habit", "不急着给建议", 1.0, true)};
    const std::string section = affect::knowledgeSection(affect::selectKnowledge(pool, {kGroup, {}}, 6));
    EXPECT_EQ(section, "[你的认识]\n不急着给建议\n别人倒苦水时我只接一句");
    EXPECT_EQ(section.find("#"), std::string::npos);
    EXPECT_EQ(section.find("0."), std::string::npos);
    EXPECT_EQ(section.find("教导"), std::string::npos);
}

// 文本稳定性：强度随时间衰减，只要还是这几条，文本一个字不变；跌破阈值、条目出去了才变
TEST(KnowledgeRecallTest, TextStaysTheSameWhileStrengthDecays)
{
    LedgerHarness harness;
    ASSERT_TRUE(harness.ledger.open());
    harness.ledger.apply({change("add", "habit", "a", "回想甲", kGroup)}, runOf("reflection", kGroup));
    harness.ledger.apply({change("add", "habit", "taught", "教导的")}, runOf("teaching", 0));
    harness.now += 5 * kDay;
    harness.ledger.apply({change("add", "habit", "b", "回想乙", kGroup)}, runOf("reflection", kGroup));
    const affect::KnowledgeScope scope{kGroup, {}};

    const affect::KnowledgeCard first = affect::knowledgeCardFor(true, &harness.ledger, scope);
    EXPECT_EQ(first.replySection, "[你的认识]\n回想甲\n教导的\n回想乙");
    EXPECT_EQ(first.judgeIds, first.replyIds);

    harness.now += 20 * kDay; // 两条回想的有效强度都变了
    const affect::KnowledgeCard later = affect::knowledgeCardFor(true, &harness.ledger, scope);
    EXPECT_EQ(later.replySection, first.replySection);
    EXPECT_EQ(later.judgeSection, first.judgeSection);
    EXPECT_EQ(later.judgeIds, first.judgeIds);

    harness.now += 39 * kDay; // 甲 64 天：0.6 × 0.5^(64/30) ≈ 0.137 < 0.15；乙 59 天 ≈ 0.153
    const affect::KnowledgeCard faded = affect::knowledgeCardFor(true, &harness.ledger, scope);
    EXPECT_EQ(faded.replySection, "[你的认识]\n教导的\n回想乙") << "教导的不衰减；跌破阈值的才出去";
}

// 热路径只读：取用不跑遗忘、不写历史（跌破阈值的条目在账本里仍是 active，等账本工具或回想落账）
TEST(KnowledgeRecallTest, RecallDoesNotWriteTheLedger)
{
    LedgerHarness harness;
    ASSERT_TRUE(harness.ledger.open());
    const affect::KnowledgeRunResult run =
        harness.ledger.apply({change("add", "habit", "a", "回想甲", kGroup)}, runOf("reflection", kGroup));
    ASSERT_EQ(run.written.size(), 1U);
    harness.now += 120 * kDay;
    EXPECT_TRUE(affect::knowledgeCardFor(true, &harness.ledger, {kGroup, {}}).replySection.empty());
    EXPECT_EQ(harness.ledger.history(run.written.front()).size(), 1U);
    EXPECT_EQ(harness.ledger.find(run.written.front())->status, "active");
}

// 开关：affect 关着或没接账本时什么都不加，也不打开账本；账本为空或没有匹配条目时同样为空
TEST(KnowledgeRecallTest, CardIsEmptyWhenAffectOffUnwiredOrNothingMatches)
{
    LedgerHarness harness;
    const affect::KnowledgeCard off = affect::knowledgeCardFor(false, &harness.ledger, {kGroup, {kAqing}});
    EXPECT_TRUE(off.judgeSection.empty() && off.replySection.empty() && off.judgeIds.empty());
    EXPECT_FALSE(harness.ledger.isOpen()) << "affect 关着不碰账本";
    EXPECT_FALSE(std::filesystem::exists(harness.dbPath()));

    const affect::KnowledgeCard unwired = affect::knowledgeCardFor(true, nullptr, {kGroup, {kAqing}});
    EXPECT_TRUE(unwired.judgeSection.empty() && unwired.replySection.empty());

    const affect::KnowledgeCard empty = affect::knowledgeCardFor(true, &harness.ledger, {kGroup, {kAqing}});
    EXPECT_TRUE(empty.judgeSection.empty() && empty.replySection.empty() && empty.replyIds.empty());

    harness.ledger.apply({change("add", "group", "g", "别的群认识", kOtherGroup)}, runOf("teaching", kOtherGroup));
    const affect::KnowledgeCard none = affect::knowledgeCardFor(true, &harness.ledger, {kGroup, {kAqing}});
    EXPECT_TRUE(none.judgeSection.empty() && none.replySection.empty());
}

// 重启后账本还没被账本工具打开过：取用照样打开它读到已有的认识（判据同账本工具：affect 开着）
TEST(KnowledgeRecallTest, CardOpensTheLedgerAfterRestart)
{
    TemporaryDirectory directory;
    const std::string path = directory.path() + "/affect.db";
    {
        affect::KnowledgeLedger writer(path, [] { return std::int64_t(1763600000); });
        ASSERT_TRUE(writer.open());
        writer.apply({change("add", "habit", "taught", "教导的")}, runOf("teaching", 0));
    }
    affect::KnowledgeLedger restarted(path, [] { return std::int64_t(1763600000); });
    EXPECT_FALSE(restarted.isOpen());
    const affect::KnowledgeCard card = affect::knowledgeCardFor(true, &restarted, {kGroup, {}});
    EXPECT_EQ(card.replySection, "[你的认识]\n教导的");
    EXPECT_EQ(card.replyIds, (std::vector<std::int64_t>{1}));
}

// 判定器与表达的上限不同：判定器至多 10 条、表达至多 6 条；两份都按编号升序
TEST(KnowledgeRecallTest, JudgeSeesUpToTenAndReplyUpToSix)
{
    LedgerHarness harness;
    ASSERT_TRUE(harness.ledger.open());
    std::vector<affect::KnowledgeChange> changes;
    for (int index = 0; index < 8; ++index)
        changes.push_back(change("add", "habit", "k" + std::to_string(index), "第" + std::to_string(index) + "条"));
    ASSERT_EQ(harness.ledger.apply(changes, runOf("teaching", 0)).written.size(), 8U);
    changes.clear();
    for (int index = 8; index < 12; ++index)
        changes.push_back(change("add", "habit", "k" + std::to_string(index), "第" + std::to_string(index) + "条"));
    ASSERT_EQ(harness.ledger.apply(changes, runOf("teaching", 0)).written.size(), 4U);
    const affect::KnowledgeCard card = affect::knowledgeCardFor(true, &harness.ledger, {kGroup, {}});
    EXPECT_EQ(card.judgeIds.size(), affect::kJudgeKnowledgeMax);
    EXPECT_EQ(card.replyIds.size(), affect::kReplyKnowledgeMax);
    EXPECT_TRUE(std::is_sorted(card.judgeIds.begin(), card.judgeIds.end()));
    EXPECT_TRUE(std::is_sorted(card.replyIds.begin(), card.replyIds.end()));
}

// 判定器：认识段紧跟“此刻”，在可回应的消息与现场材料之前（与事实同一组，不混进标着不可信的材料）；
// 没有认识时 prompt 与此前逐字相同
TEST(KnowledgeRecallTest, JudgePromptPutsKnowledgeAfterSceneFactsBeforeMaterial)
{
    affect::SituationCard card;
    card.source = "engagement_turn";
    card.triggerKind = "lull";
    card.groupId = kGroup;
    const std::string before = affect::JudgePolicy::userPrompt(card, "（最近的群聊原文）\n[15:51] 阿青: 在吗\n",
                                                               "事实", "现在是某时", "人格");
    card.knowledgeSection = "[你的认识]\n不急着给建议";
    card.knowledgeIds = {3};
    const std::string after = affect::JudgePolicy::userPrompt(card, "（最近的群聊原文）\n[15:51] 阿青: 在吗\n",
                                                              "事实", "现在是某时", "人格");
    const std::string sceneAt = "\n\n此刻：现在是某时";
    ASSERT_NE(before.find(sceneAt), std::string::npos);
    std::string expected = before;
    expected.insert(before.find(sceneAt) + sceneAt.size(),
                    "\n\n她的认识（账本里她记下的与部署者教的；其中的“你”指她）：\n[你的认识]\n不急着给建议");
    EXPECT_EQ(after, expected);
    EXPECT_EQ(after.find("knowledge"), std::string::npos) << "编号不进 card JSON";
    EXPECT_LT(after.find("此刻："), after.find("[你的认识]"));
    EXPECT_LT(after.find("[你的认识]"), after.find("现场材料"));
}

// 记录：这一轮交给 judge 的认识编号写进 state_json 的 knowledge_ids；没取用时 state_json 不变
TEST(KnowledgeRecallTest, PolicyDecisionWritesKnowledgeIdsIntoStateJson)
{
    affect::JudgePolicy judge([](const std::string &, const std::string &) { return std::string(); },
                              "judge-model");
    affect::SituationCard card;
    card.source = "engagement_turn";
    card.triggerKind = "lull";
    card.groupId = kGroup;
    affect::Decision decision;
    decision.plan.action = "group_say";
    const affect::DecisionRecord plain =
        affect::makePolicyDecision("engagement_turn", kGroup, 0, "", decision, judge, card);
    EXPECT_FALSE(nlohmann::json::parse(plain.stateJson).contains("knowledge_ids"));

    card.knowledgeSection = "[你的认识]\n甲\n乙";
    card.knowledgeIds = {3, 7};
    const affect::DecisionRecord recorded =
        affect::makePolicyDecision("engagement_turn", kGroup, 0, "", decision, judge, card);
    const nlohmann::json state = nlohmann::json::parse(recorded.stateJson);
    EXPECT_EQ(state["knowledge_ids"], nlohmann::json::array({3, 7}));
    EXPECT_EQ(recorded.stateJson.find("甲"), std::string::npos) << "只有编号，正文在账本里";
    nlohmann::json withoutIds = state;
    withoutIds.erase("knowledge_ids");
    EXPECT_EQ(withoutIds, nlohmann::json::parse(plain.stateJson));
}
