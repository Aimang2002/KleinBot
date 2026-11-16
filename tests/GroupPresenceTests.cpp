#include <gtest/gtest.h>

#include "Affect/GroupPresence.h"

#include <cstdint>
#include <utility>
#include <vector>

using affect::GroupPresence;
using affect::PresenceRules;
using affect::SpeechKind;

namespace
{
constexpr std::int64_t kStart = 1'000'000;
}

// 窗口里没有任何别人的消息：没有可接的话，不主动开口
TEST(GroupPresenceTest, NoOthersMeansNoVolunteering)
{
    GroupPresence presence;
    EXPECT_FALSE(presence.mayVolunteer(kStart));
    presence.noteSpeech(kStart, SpeechKind::Answer);
    EXPECT_FALSE(presence.mayVolunteer(kStart + 1)) << "她自己的应答不算可接的话";
}

// 一对一：对方说一句，她能主动接一句；再接要等对方再说
TEST(GroupPresenceTest, OneOnOneAllowsOneLinePerLine)
{
    GroupPresence presence;
    presence.noteOthersMessage(kStart, 42);
    EXPECT_TRUE(presence.mayVolunteer(kStart + 1));
    presence.noteSpeech(kStart + 2, SpeechKind::Unprompted);
    EXPECT_FALSE(presence.mayVolunteer(kStart + 3));
    presence.noteOthersMessage(kStart + 10, 42);
    EXPECT_TRUE(presence.mayVolunteer(kStart + 11));
}

// 她自己开口永远不会让份额变多（2026-11-16 机制错误一：额度被它要限制的东西续上）
TEST(GroupPresenceTest, HerOwnSpeechNeverRefillsTheShare)
{
    GroupPresence presence;
    for (int index = 0; index < 4; ++index)
        presence.noteOthersMessage(kStart + index, 42);
    std::int64_t now = kStart + 10;
    int spoken = 0;
    while (presence.mayVolunteer(now) && spoken < 100)
    {
        presence.noteSpeech(now, SpeechKind::Unprompted);
        ++spoken;
        now += 5;
    }
    EXPECT_EQ(spoken, 4) << "一个人说了 4 条，她至多主动说 4 条";
    for (int index = 0; index < 20; ++index)
    {
        presence.noteSpeech(now, SpeechKind::Unprompted);
        EXPECT_FALSE(presence.mayVolunteer(now)) << "越说越不能说";
        now += 5;
    }
}

// 被叫到时的应答是义务：只记账，不占份额
TEST(GroupPresenceTest, AnswersDoNotConsumeTheShare)
{
    GroupPresence presence;
    presence.noteOthersMessage(kStart, 42);
    for (int index = 0; index < 10; ++index)
        presence.noteSpeech(kStart + 1 + index, SpeechKind::Answer);
    EXPECT_TRUE(presence.mayVolunteer(kStart + 20));
    const affect::PresenceView view = presence.view(kStart + 20);
    EXPECT_EQ(view.answers, 10);
    EXPECT_EQ(view.unprompted, 0);
}

// 多人热聊：她不比一般群友话多（平均每人条数即她的主动条数上限）
TEST(GroupPresenceTest, ManySpeakersCapAtAverageParticipant)
{
    GroupPresence presence;
    // 4 个人共 24 条：平均每人 6 条
    for (int index = 0; index < 24; ++index)
        presence.noteOthersMessage(kStart + index, static_cast<std::uint64_t>(index % 4));
    std::int64_t now = kStart + 30;
    int spoken = 0;
    while (presence.mayVolunteer(now) && spoken < 100)
    {
        presence.noteSpeech(now, SpeechKind::Unprompted);
        ++spoken;
        now += 1;
    }
    EXPECT_EQ(spoken, 6);
    const affect::PresenceView view = presence.view(now);
    EXPECT_EQ(view.othersMessages, 24);
    EXPECT_EQ(view.otherSpeakers, 4);
    EXPECT_EQ(view.unprompted, 6);
}

// 份额随时间恢复：她的主动发言滑出窗口后又能开口
TEST(GroupPresenceTest, ShareRecoversAsTheWindowSlides)
{
    PresenceRules rules;
    rules.shareWindowSeconds = 600;
    GroupPresence presence(rules);
    presence.noteOthersMessage(kStart, 42);
    presence.noteSpeech(kStart + 1, SpeechKind::Unprompted);
    EXPECT_FALSE(presence.mayVolunteer(kStart + 2));
    // 她那句与对方那句一起滑出窗口：没有可接的话
    EXPECT_FALSE(presence.mayVolunteer(kStart + 700));
    presence.noteOthersMessage(kStart + 700, 43);
    EXPECT_TRUE(presence.mayVolunteer(kStart + 701)) << "旧的主动发言已不在窗口里";
    EXPECT_EQ(presence.view(kStart + 701).unprompted, 0);
}

// 退场：期间不主动开口；到期恢复；再次退场取更晚的截止
TEST(GroupPresenceTest, WithdrawalBlocksVolunteeringUntilItExpires)
{
    PresenceRules rules;
    rules.withdrawSeconds = 1800;
    GroupPresence presence(rules);
    for (int index = 0; index < 10; ++index)
        presence.noteOthersMessage(kStart + index, static_cast<std::uint64_t>(index % 2));
    ASSERT_TRUE(presence.mayVolunteer(kStart + 20));

    presence.withdraw(kStart + 20);
    EXPECT_TRUE(presence.withdrawn(kStart + 21));
    EXPECT_FALSE(presence.mayVolunteer(kStart + 21));
    presence.noteOthersMessage(kStart + 1000, 5);
    EXPECT_FALSE(presence.mayVolunteer(kStart + 1001)) << "别人说话不结束退场";

    presence.withdraw(kStart + 10); // 更早的截止不缩短退场
    EXPECT_EQ(presence.view(kStart + 1001).withdrawnUntil, kStart + 20 + 1800);

    EXPECT_FALSE(presence.withdrawn(kStart + 20 + 1800));
    presence.noteOthersMessage(kStart + 1830, 6);
    EXPECT_TRUE(presence.mayVolunteer(kStart + 1831));
}

// 2026-11-16 10:47–10:53 现场回放（只用节奏，不含原文）：4 个群友约 25 条，她此前主动说了 20 条。
// 按份额，她至多说到其他人平均每人的条数（约 6 条）就停
TEST(GroupPresenceTest, IncidentReplayStopsHerAtTheAverageParticipant)
{
    GroupPresence presence;
    // 群友消息：U1 话最多，U4 次之，U2、U3 各两三句（按到达顺序穿插她的尝试）
    const std::vector<std::pair<int, std::uint64_t>> others = {
        {0, 1},   {6, 2},   {9, 3},   {10, 3},  {52, 4},  {57, 4},  {63, 1},  {70, 1},
        {75, 1},  {90, 1},  {100, 4}, {117, 1}, {119, 4}, {134, 4}, {137, 4}, {145, 4},
        {151, 1}, {161, 4}, {167, 1}, {177, 1}, {195, 1}, {199, 1}, {213, 1}, {231, 4},
        {252, 4}};
    int volunteered = 0;
    std::size_t next = 0;
    for (std::int64_t second = 0; second <= 360; ++second)
    {
        while (next < others.size() && others[next].first == second)
        {
            presence.noteOthersMessage(kStart + second, others[next].second);
            ++next;
        }
        // 她每隔 10 秒想接一句（现场大约这个频率）
        if (second % 10 == 5 && presence.mayVolunteer(kStart + second))
        {
            presence.noteSpeech(kStart + second, SpeechKind::Unprompted);
            ++volunteered;
        }
    }
    RecordProperty("volunteered", volunteered);
    EXPECT_LE(volunteered, 7);
    EXPECT_GE(volunteered, 4) << "她仍能参与，只是不再刷屏";
}
