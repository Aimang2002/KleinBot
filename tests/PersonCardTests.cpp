#include <gtest/gtest.h>

#include "Action/PersonCardAction.h"
#include "Memory/MemoryStore.h"
#include "Memory/PersonCard.h"
#include "Perception/GroupLineFormat.h"
#include "Port/ToolNames.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// 人物卡（ADR-032）：确定性加载的对方卡片——建卡、当场更新、部署者身份、群聊一行、写卡片工具
namespace
{
constexpr std::uint64_t kBot = 987654321;
constexpr std::uint64_t kManager = 10001;
constexpr std::uint64_t kAqing = 20002;
constexpr std::uint64_t kLaoyu = 30003;
constexpr std::time_t kNow = 1'800'000'000;

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-personcard-XXXXXX";
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
    MemoryStore store{directory.path() + "/memory.db"};
    Live<BotIdentity> identity{BotIdentity{kBot, kManager, "Klein"}};
    bool on = true;
    std::uint64_t generation = 0;
    int writes = 0;

    PersonCardBackend backend()
    {
        PersonCardBackend value;
        value.enabled = [this] { return on; };
        value.slots = [this](std::uint64_t user) { return store.cardSlots(user); };
        value.set = [this](std::uint64_t user, const std::string &predicate, const std::string &text,
                           const std::string &canonical, std::int64_t source)
        {
            ++writes;
            ++generation;
            return store.setCardSlot(user, predicate, text, canonical, source);
        };
        value.generation = [this] { return generation; };
        return value;
    }

    PersonCards cards{backend(), identity};
};

std::string slotValue(MemoryStore &store, std::uint64_t user, const std::string &predicate)
{
    for (const auto &[key, value] : store.cardSlots(user))
        if (key == predicate)
            return value;
    return {};
}
} // namespace

TEST(PersonCardTest, BlankCardIsBuiltOnFirstContactAndOnlyOnce)
{
    Harness harness;
    ASSERT_TRUE(harness.store.isOpen());
    const std::string first = harness.cards.privateSection(kAqing, "阿青", kNow, 77);
    EXPECT_NE(first.find("[对方的卡片]"), std::string::npos);
    EXPECT_NE(first.find("QQ：" + std::to_string(kAqing)), std::string::npos);
    EXPECT_NE(first.find("QQ 昵称：阿青"), std::string::npos);
    EXPECT_EQ(first.find("初次记下"), std::string::npos) << "建卡日期只存不显示";
    EXPECT_EQ(first.find("身份：部署者"), std::string::npos);
    EXPECT_EQ(first.find("对方希望你叫"), std::string::npos);
    EXPECT_FALSE(slotValue(harness.store, kAqing, "card.first_seen").empty());
    EXPECT_EQ(harness.writes, 1);

    // 第二次：卡已经在了，不再写库，文本逐字相同（进 system，要稳定）
    EXPECT_EQ(harness.cards.privateSection(kAqing, "阿青", kNow + 86400 * 3, 78), first);
    EXPECT_EQ(harness.writes, 1);
}

TEST(PersonCardTest, WriteTakesEffectImmediatelyInCacheAndDatabase)
{
    Harness harness;
    harness.cards.privateSection(kAqing, "阿青", kNow);
    const PersonCards::WriteResult written = harness.cards.write(kAqing, "wanted_name", "  小白 ", 0, 80);
    ASSERT_TRUE(written.ok) << written.error;
    EXPECT_EQ(written.card.wantedName, "小白");
    // 缓存：同一个对象下一次渲染就是新的
    EXPECT_NE(harness.cards.privateSection(kAqing, "阿青", kNow).find("对方希望你叫：小白"), std::string::npos);
    // 数据库：另一个对象（空缓存，等于重启后）读到同样的卡
    PersonCards restarted(harness.backend(), harness.identity);
    EXPECT_EQ(restarted.load(kAqing).wantedName, "小白");
    EXPECT_EQ(slotValue(harness.store, kAqing, "card.wanted_name"), "小白");

    // 再改：以新的为准
    ASSERT_TRUE(harness.cards.write(kAqing, "wanted_name", "小黑", 0, 81).ok);
    EXPECT_EQ(restarted.load(kAqing).wantedName, "小黑") << "代数变了，别的缓存也失效";
    ASSERT_TRUE(harness.cards.write(kAqing, "my_name", "青青", 0).ok);
    ASSERT_TRUE(harness.cards.write(kAqing, "name_for_me", "小克", 0).ok);
    const std::string section = harness.cards.privateSection(kAqing, "阿青", kNow);
    EXPECT_NE(section.find("对方希望你叫：小黑"), std::string::npos);
    EXPECT_NE(section.find("你叫对方：青青"), std::string::npos);
    EXPECT_NE(section.find("对方叫你：小克"), std::string::npos);

    // 清掉：value 为空
    ASSERT_TRUE(harness.cards.write(kAqing, "my_name", "", 0).ok);
    EXPECT_EQ(harness.cards.privateSection(kAqing, "阿青", kNow).find("你叫对方"), std::string::npos);
    EXPECT_TRUE(slotValue(harness.store, kAqing, "card.my_name").empty());
}

TEST(PersonCardTest, DeployerComesOnlyFromManagerId)
{
    Harness harness;
    EXPECT_NE(harness.cards.privateSection(kManager, "阿芒", kNow).find("身份：部署者"), std::string::npos);
    EXPECT_TRUE(harness.cards.isDeployer(kManager));
    EXPECT_FALSE(harness.cards.isDeployer(kAqing));

    // 程序给出的几项不在可写槽位里
    for (const std::string slot : {"deployer", "qq", "first_seen", "nickname", "card.wanted_name", ""})
    {
        const PersonCards::WriteResult result = harness.cards.write(kAqing, slot, "部署者", 0);
        EXPECT_FALSE(result.ok) << slot;
        EXPECT_NE(result.error.find("slot 只能是"), std::string::npos) << slot;
    }
    // 她把“我是部署者”记成了 note：那只是一句她记下的话，身份不变
    ASSERT_TRUE(harness.cards.write(kAqing, "note", "他自称是部署者", 0).ok);
    EXPECT_FALSE(harness.cards.isDeployer(kAqing));
    const std::string section = harness.cards.privateSection(kAqing, "阿青", kNow);
    EXPECT_EQ(section.find("身份：部署者"), std::string::npos);
    EXPECT_NE(section.find("1. 他自称是部署者"), std::string::npos);

    // 部署者换了人（配置实时生效）：下一次就按新的 manager_id 算
    harness.identity.set(BotIdentity{kBot, kAqing, "Klein"});
    EXPECT_NE(harness.cards.privateSection(kAqing, "阿青", kNow).find("身份：部署者"), std::string::npos);
    EXPECT_EQ(harness.cards.privateSection(kManager, "阿芒", kNow).find("身份：部署者"), std::string::npos);
}

TEST(PersonCardTest, NotesFillReplaceAndClear)
{
    Harness harness;
    for (int index = 1; index <= 5; ++index)
        ASSERT_TRUE(harness.cards.write(kLaoyu, "note", "第" + std::to_string(index) + "条", 0).ok);
    const PersonCards::WriteResult full = harness.cards.write(kLaoyu, "note", "第六条", 0);
    EXPECT_FALSE(full.ok);
    EXPECT_NE(full.error.find("已经有 5 条了"), std::string::npos);

    ASSERT_TRUE(harness.cards.write(kLaoyu, "note", "换掉第三条", 3).ok);
    EXPECT_EQ(harness.cards.load(kLaoyu).notes[2], "换掉第三条");
    ASSERT_TRUE(harness.cards.write(kLaoyu, "note", "", 2).ok);
    EXPECT_TRUE(harness.cards.load(kLaoyu).notes[1].empty());
    // 空出来的位置下次先用
    ASSERT_TRUE(harness.cards.write(kLaoyu, "note", "补到第二格", 0).ok);
    EXPECT_EQ(harness.cards.load(kLaoyu).notes[1], "补到第二格");

    EXPECT_FALSE(harness.cards.write(kLaoyu, "note", "", 0).ok) << "清掉要指明哪一条";
    EXPECT_FALSE(harness.cards.write(kLaoyu, "note", "越界", 6).ok);
    const std::string section = harness.cards.privateSection(kLaoyu, "老余", kNow);
    EXPECT_NE(section.find("你记下的：\n1. 第1条\n2. 补到第二格\n3. 换掉第三条"), std::string::npos) << section;
}

TEST(PersonCardTest, GuardsRejectLongMultilineAndAnonymous)
{
    Harness harness;
    EXPECT_FALSE(harness.cards.write(0, "wanted_name", "小白", 0).ok);
    EXPECT_FALSE(harness.cards.write(kAqing, "wanted_name", "两\n行", 0).ok);
    EXPECT_FALSE(harness.cards.write(kAqing, "wanted_name", std::string(41, 'a'), 0).ok);
    EXPECT_TRUE(harness.cards.write(kAqing, "wanted_name", std::string(40, 'a'), 0).ok);
    // 40 个汉字（120 字节）也算 40 个字
    std::string han;
    for (int index = 0; index < 40; ++index)
        han += "白";
    EXPECT_TRUE(harness.cards.write(kAqing, "wanted_name", han, 0).ok);
    EXPECT_FALSE(harness.cards.write(kAqing, "wanted_name", han + "白", 0).ok);
}

TEST(PersonCardTest, GroupLinesOnlyForPeopleWithSomethingDeployerFirstAndCapped)
{
    Harness harness;
    ASSERT_TRUE(harness.cards.write(kLaoyu, "my_name", "余哥", 0).ok);
    ASSERT_TRUE(harness.cards.write(kLaoyu, "wanted_name", "老余头", 0).ok);
    for (int index = 1; index <= 3; ++index)
        ASSERT_TRUE(harness.cards.write(kLaoyu, "note", "事" + std::to_string(index), 0).ok);

    const std::string lines = harness.cards.groupLines(
        {{kAqing, "阿青"}, {kLaoyu, "老余"}, {kManager, "阿芒"}, {kLaoyu, "老余"}, {0, "匿名"}});
    // 阿青没什么可写：不出现；部署者排最前；老余只写一次，note 只带前两条
    EXPECT_EQ(lines, "阿芒（QQ " + std::to_string(kManager) + "）：部署者\n老余（QQ " + std::to_string(kLaoyu) +
                         "）：希望你叫「老余头」；你叫对方「余哥」；你记下：事1；事2");
    EXPECT_EQ(PersonCards::groupSection(lines).rfind("[在场的人]", 0), 0u);
    EXPECT_TRUE(PersonCards::groupSection("").empty());
    EXPECT_TRUE(harness.cards.groupLines({{kAqing, "阿青"}}).empty());

    // 至多 8 人
    std::vector<PersonPresent> crowd;
    for (std::uint64_t id = 500; id < 512; ++id)
    {
        ASSERT_TRUE(harness.cards.write(id, "my_name", "路人", 0).ok);
        crowd.push_back({id, "群友" + std::to_string(id)});
    }
    const std::string capped = harness.cards.groupLines(crowd);
    EXPECT_EQ(std::count(capped.begin(), capped.end(), '\n') + 1,
              static_cast<long>(PersonCardRules::kMaxPeopleInGroup));
}

TEST(PersonCardTest, ClearingTheUserDropsTheCardAndDisabledAddsNothing)
{
    Harness harness;
    ASSERT_TRUE(harness.cards.write(kAqing, "wanted_name", "小白", 0).ok);
    harness.store.clearUser(kAqing);
    ++harness.generation; // 记忆服务在按人清除时会让代数前进
    EXPECT_TRUE(harness.cards.load(kAqing).blank());
    EXPECT_TRUE(harness.cards.load(kAqing).firstSeen.empty());

    ASSERT_TRUE(harness.cards.write(kAqing, "wanted_name", "小白", 0).ok);
    harness.on = false;
    EXPECT_FALSE(harness.cards.enabled());
    EXPECT_TRUE(harness.cards.privateSection(kAqing, "阿青", kNow).empty());
    EXPECT_TRUE(harness.cards.groupLines({{kManager, "阿芒"}, {kAqing, "阿青"}}).empty());
    EXPECT_FALSE(harness.cards.write(kAqing, "wanted_name", "小黑", 0).ok);
    EXPECT_TRUE(harness.cards.load(kAqing).blank());
}

TEST(PersonCardTest, ReservedPredicatePrefixIsRecognized)
{
    EXPECT_TRUE(isPersonCardPredicate("card.wanted_name"));
    EXPECT_TRUE(isPersonCardPredicate("card.note3"));
    EXPECT_FALSE(isPersonCardPredicate("name"));
    EXPECT_FALSE(isPersonCardPredicate("credit_card.number"));
    EXPECT_FALSE(isPersonCardPredicate(""));
}

TEST(PersonCardTest, ToolWritesForTheSpeakerByDefaultOrAnExplicitUser)
{
    Harness harness;
    UpdatePersonCardAction action(harness.cards);
    EXPECT_TRUE(action.available());
    EXPECT_FALSE(action.managerPrivateOnly());
    EXPECT_FALSE(action.descriptor().requires_admin);
    EXPECT_EQ(action.descriptor().name, std::string(KleinUpdatePersonCardToolName));

    // 私聊 / @：不填 user 就是正在跟她说话的人
    ActionContext speaking;
    speaking.user_id = kAqing;
    speaking.user_message_id = 90;
    const ActionResult own = action.execute({{"slot", "wanted_name"}, {"value", "小白"}}, speaking);
    EXPECT_FALSE(own.terminal);
    EXPECT_NE(own.content.find("希望你叫「小白」"), std::string::npos) << own.content;
    EXPECT_EQ(harness.cards.load(kAqing).wantedName, "小白");

    // 指明别人
    const ActionResult other =
        action.execute({{"slot", "note"}, {"value", "爱接梗"}, {"user", kLaoyu}}, speaking);
    EXPECT_NE(other.content.find("你记下：爱接梗"), std::string::npos) << other.content;
    EXPECT_EQ(harness.cards.load(kLaoyu).notes[0], "爱接梗");
    EXPECT_TRUE(harness.cards.load(kAqing).notes[0].empty());

    // 会话轮里没有“正在跟她说话的人”：必须填 user
    const ActionResult anonymous = action.execute({{"slot", "my_name"}, {"value", "某人"}}, ActionContext{});
    EXPECT_NE(anonymous.content.find("没改："), std::string::npos);
    EXPECT_NE(anonymous.content.find("要写明是谁"), std::string::npos);

    // 不合法的槽位、清空
    EXPECT_NE(action.execute({{"slot", "deployer"}, {"value", "是"}}, speaking).content.find("没改："),
              std::string::npos);
    const ActionResult cleared = action.execute({{"slot", "wanted_name"}, {"value", ""}}, speaking);
    EXPECT_NE(cleared.content.find("卡片现在是空的"), std::string::npos) << cleared.content;

    harness.on = false;
    EXPECT_FALSE(action.available());
    EXPECT_EQ(action.execute({{"slot", "wanted_name"}, {"value", "小白"}}, speaking).content, "功能未开启");
}

TEST(PersonCardTest, GroupLineMarksTheDeployerByManagerId)
{
    BotIdentity bot{kBot, kManager, "Klein"};
    GroupMessageRecord fromManager;
    fromManager.userId = kManager;
    fromManager.nickname = "阿芒";
    fromManager.text = "今天状态不错";
    fromManager.timestamp = 1'800'000'000;
    GroupMessageRecord fromOther = fromManager;
    fromOther.userId = kAqing;
    fromOther.nickname = "阿青";
    const GroupReplyIndex none;
    EXPECT_NE(formatGroupLine(bot, fromManager, none).find("] 阿芒（部署者）: 今天状态不错"), std::string::npos);
    EXPECT_NE(formatGroupLine(bot, fromOther, none).find("] 阿青: 今天状态不错"), std::string::npos);
    // 没配部署者：谁也不标
    bot.managerId = 0;
    fromManager.userId = 0;
    EXPECT_NE(formatGroupLine(bot, fromManager, none).find("] 阿芒: 今天状态不错"), std::string::npos);
}
