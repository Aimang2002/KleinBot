#include <gtest/gtest.h>

#include "Action/CancelReminderAction.h"
#include "Action/ListRemindersAction.h"
#include "Action/SetReminderAction.h"
#include "Persistence/ReminderStore.h"
#include "Reminder/ReminderService.h"
#include "Reminder/ReminderTime.h"

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
        std::string pattern = "/tmp/kleinbot-reminder-tests-XXXXXX";
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

// 未来一小时的本地 ISO 时间，保证 set 校验稳定通过
std::string futureIsoTime()
{
    return formatLocal(nowSeconds() + 3600);
}
}

TEST(ReminderTimeTest, ParsesIsoLocalWithTSeparatorAndSpaceSeparator)
{
    const auto withT = parseIsoLocal("2026-08-20T09:30");
    const auto withSpace = parseIsoLocal("2026-08-20 09:30");
    ASSERT_TRUE(withT.has_value());
    ASSERT_TRUE(withSpace.has_value());
    EXPECT_EQ(*withT, *withSpace);
    EXPECT_EQ(formatLocal(*withT), "2026-08-20 09:30");
}

TEST(ReminderTimeTest, RejectsInvalidDatesAndFormats)
{
    EXPECT_FALSE(parseIsoLocal("2026-02-30T09:30").has_value()); // 2 月 30 日
    EXPECT_FALSE(parseIsoLocal("2026-13-01T09:30").has_value()); // 月越界
    EXPECT_FALSE(parseIsoLocal("2026-08-20T24:00").has_value()); // 时越界
    EXPECT_FALSE(parseIsoLocal("2026-08-20T9:30").has_value());  // 长度不符
    EXPECT_FALSE(parseIsoLocal("2026/08/20 09:30").has_value()); // 分隔符不符
    EXPECT_FALSE(parseIsoLocal("").has_value());
}

TEST(ReminderTimeTest, RollsRecurringOccurrenceStrictlyAfterNow)
{
    constexpr int64_t daily = 86400;
    const int64_t scheduledAt = 1000;
    // 未到期：保持原时间
    EXPECT_EQ(nextOccurrenceAfter(scheduledAt, "daily", 900), scheduledAt);
    // 已过期 10 小时：滚动一天
    EXPECT_EQ(nextOccurrenceAfter(scheduledAt, "daily", scheduledAt + 36000),
              scheduledAt + daily);
    // 已过期一天多：滚动两天
    EXPECT_EQ(nextOccurrenceAfter(scheduledAt, "daily", scheduledAt + daily + 1),
              scheduledAt + 2 * daily);
    EXPECT_EQ(nextOccurrenceAfter(scheduledAt, "weekly", scheduledAt + 1),
              scheduledAt + 604800);
    EXPECT_EQ(nextOccurrenceAfter(scheduledAt, "none", scheduledAt + 1), 0);
}

TEST(ReminderStoreIntegrationTest, InsertsListsCountsDeletesAndUpdates)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    ReminderStore store(temporaryDirectory.path() + "/reminders.db");

    const int64_t first = store.insert(10, "first", 2000, "none");
    const int64_t second = store.insert(10, "second", 1000, "daily");
    const int64_t other = store.insert(20, "other", 1500, "none");
    EXPECT_GT(first, 0);
    EXPECT_GT(second, 0);
    EXPECT_GT(other, 0);

    // listForUser 按 trigger_at 升序，用户隔离
    const auto userTen = store.listForUser(10);
    ASSERT_EQ(userTen.size(), 2U);
    EXPECT_EQ(userTen[0].content, "second");
    EXPECT_EQ(userTen[0].repeat_rule, "daily");
    EXPECT_EQ(userTen[1].content, "first");
    EXPECT_EQ(store.listForUser(20).size(), 1U);
    EXPECT_EQ(store.countForUser(10), 2U);

    EXPECT_TRUE(store.updateTrigger(second, 3000));
    const auto updated = store.listForUser(10);
    EXPECT_EQ(updated[1].trigger_at, 3000);

    EXPECT_TRUE(store.deleteById(first));
    EXPECT_FALSE(store.deleteById(first));
    EXPECT_EQ(store.countForUser(10), 1U);

    // loadPending 返回全部用户，按 trigger_at 升序
    const auto pending = store.loadPending();
    ASSERT_EQ(pending.size(), 2U);
    EXPECT_LE(pending[0].trigger_at, pending[1].trigger_at);
}

TEST(ReminderServiceTest, FiresOneShotOnceAndRollsDailyForward)
{
    TemporaryDirectory temporaryDirectory;
    ReminderStore store(temporaryDirectory.path() + "/reminders.db");
    ReminderService service(store);

    const int64_t now = nowSeconds();
    const int64_t oneShotAt = now + 60;
    const int64_t dailyAt = now + 120;
    const int64_t oneShotId = service.add(10, "one-shot", oneShotAt, "none");
    const int64_t dailyId = service.add(10, "daily-item", dailyAt, "daily");
    EXPECT_GT(oneShotId, 0);
    EXPECT_GT(dailyId, 0);

    // 未到期不出队
    EXPECT_TRUE(service.popDue(now).empty());

    // 到期：一次性删除，每日滚动到下一天
    const auto due = service.popDue(oneShotAt + 1);
    ASSERT_EQ(due.size(), 1U);
    EXPECT_EQ(due[0].id, oneShotId);
    EXPECT_EQ(due[0].content, "one-shot");
    EXPECT_FALSE(due[0].late);
    const auto second = service.popDue(dailyAt + 1);
    ASSERT_EQ(second.size(), 1U);
    EXPECT_EQ(second[0].id, dailyId);
    // 返回的是刚触发的事件，scheduled_at 为本次原定时间；下一次由 list 断言覆盖
    EXPECT_EQ(second[0].scheduled_at, dailyAt);

    // 一次性已删行，每日仍在队列且滚动后不会再触发
    const auto remaining = service.list(10);
    ASSERT_EQ(remaining.size(), 1U);
    EXPECT_EQ(remaining[0].id, dailyId);
    EXPECT_EQ(remaining[0].trigger_at, dailyAt + 86400);
    EXPECT_TRUE(service.popDue(dailyAt + 1).empty());
    EXPECT_FALSE(service.cancel(10, oneShotId));
}

TEST(ReminderServiceTest, SameSecondEventsBothFire)
{
    TemporaryDirectory temporaryDirectory;
    ReminderStore store(temporaryDirectory.path() + "/reminders.db");
    ReminderService service(store);

    const int64_t now = nowSeconds();
    const int64_t trigger = now + 60;
    service.add(10, "first-at-same-second", trigger, "none");
    service.add(20, "second-at-same-second", trigger, "none");

    const auto due = service.popDue(trigger);
    ASSERT_EQ(due.size(), 2U);
    EXPECT_EQ(due[0].content, "first-at-same-second");
    EXPECT_EQ(due[1].content, "second-at-same-second");
    EXPECT_TRUE(service.popDue(trigger).empty());
}

TEST(ReminderServiceTest, CancelValidatesOwnership)
{
    TemporaryDirectory temporaryDirectory;
    ReminderStore store(temporaryDirectory.path() + "/reminders.db");
    ReminderService service(store);

    const int64_t id = service.add(10, "mine", nowSeconds() + 600, "none");
    EXPECT_GT(id, 0);
    EXPECT_FALSE(service.cancel(20, id));
    EXPECT_TRUE(service.cancel(10, id));
    EXPECT_TRUE(service.list(10).empty());
}

TEST(ReminderServiceTest, RecoversMissedOneShotWithinWindowAndDropsBeyond)
{
    TemporaryDirectory temporaryDirectory;
    const std::string dbPath = temporaryDirectory.path() + "/reminders.db";
    const int64_t now = nowSeconds();
    {
        // 直接写库模拟重启前的存量数据
        ReminderStore store(dbPath);
        store.insert(10, "missed-recently", now - 3600, "none");
        store.insert(20, "missed-too-late", now - 90000, "none");
    }
    ReminderStore store(dbPath);
    ReminderService service(store);

    const auto due = service.popDue(now);
    ASSERT_EQ(due.size(), 1U);
    EXPECT_EQ(due[0].content, "missed-recently");
    EXPECT_TRUE(due[0].late);
    // 超窗事件被丢弃，两条都不再存在
    EXPECT_TRUE(store.loadPending().empty());
}

TEST(ReminderServiceTest, RecoversMissedDailyByFiringOnceThenRolling)
{
    TemporaryDirectory temporaryDirectory;
    const std::string dbPath = temporaryDirectory.path() + "/reminders.db";
    const int64_t now = nowSeconds();
    const int64_t missedAt = now - 3600;
    {
        ReminderStore store(dbPath);
        store.insert(10, "daily-missed", missedAt, "daily");
    }
    ReminderStore store(dbPath);
    ReminderService service(store);

    const auto due = service.popDue(now);
    ASSERT_EQ(due.size(), 1U);
    EXPECT_EQ(due[0].content, "daily-missed");
    EXPECT_TRUE(due[0].late);
    EXPECT_EQ(due[0].scheduled_at, missedAt);

    // 补发一次后滚动到下一次（严格晚于 now），不再立即触发
    EXPECT_TRUE(service.popDue(now).empty());
    const auto remaining = service.list(10);
    ASSERT_EQ(remaining.size(), 1U);
    EXPECT_GT(remaining[0].trigger_at, now);
    EXPECT_EQ(remaining[0].repeat_rule, "daily");
}

TEST(SetReminderActionTest, RegistersValidReminderAndReportsId)
{
    TemporaryDirectory temporaryDirectory;
    ReminderStore store(temporaryDirectory.path() + "/reminders.db");
    ReminderService service(store);
    SetReminderAction action(service);

    const std::string iso = futureIsoTime();
    const ActionResult result = action.execute(
        {{"content", "喝水"}, {"time", iso}, {"repeat", "daily"}},
        {10, 0, "每天早上提醒我喝水"});
    EXPECT_NE(result.content.find("提醒已设置"), std::string::npos);
    EXPECT_NE(result.content.find("每天重复"), std::string::npos);
    ASSERT_EQ(service.list(10).size(), 1U);
    EXPECT_EQ(service.list(10)[0].content, "喝水");
    EXPECT_EQ(service.list(10)[0].repeat_rule, "daily");
}

TEST(SetReminderActionTest, RejectsInvalidTimeContentAndRepeat)
{
    TemporaryDirectory temporaryDirectory;
    ReminderStore store(temporaryDirectory.path() + "/reminders.db");
    ReminderService service(store);
    SetReminderAction action(service);
    const ActionContext context{10, 0, ""};

    EXPECT_NE(action.execute({{"content", "x"}, {"time", "not-a-time"}}, context)
                  .content.find("错误"),
              std::string::npos);
    EXPECT_NE(action.execute({{"content", "x"},
                              {"time", formatLocal(nowSeconds() - 3600)}},
                             context)
                  .content.find("错误"),
              std::string::npos);
    EXPECT_NE(action.execute({{"content", "   "}, {"time", futureIsoTime()}}, context)
                  .content.find("错误"),
              std::string::npos);
    EXPECT_NE(action.execute({{"content", "x"},
                              {"time", futureIsoTime()},
                              {"repeat", "monthly"}},
                             context)
                  .content.find("错误"),
              std::string::npos);
    EXPECT_TRUE(service.list(10).empty());
}

TEST(SetReminderActionTest, EnforcesPerUserPendingCap)
{
    TemporaryDirectory temporaryDirectory;
    ReminderStore store(temporaryDirectory.path() + "/reminders.db");
    ReminderService service(store);
    SetReminderAction action(service);

    for (std::size_t index = 0; index < ReminderService::kMaxPendingPerUser; ++index)
    {
        // 同一用户不同触发时间各注册一条
        const ActionResult result = action.execute(
            {{"content", "item"}, {"time", formatLocal(nowSeconds() + 3600 + index)}},
            {10, 0, ""});
        EXPECT_NE(result.content.find("提醒已设置"), std::string::npos);
    }
    const ActionResult rejected = action.execute(
        {{"content", "overflow"}, {"time", futureIsoTime()}}, {10, 0, ""});
    EXPECT_NE(rejected.content.find("上限"), std::string::npos);
    EXPECT_EQ(service.pendingCount(10), ReminderService::kMaxPendingPerUser);

    // 其他用户不受影响
    const ActionResult otherUser = action.execute(
        {{"content", "other"}, {"time", futureIsoTime()}}, {20, 0, ""});
    EXPECT_NE(otherUser.content.find("提醒已设置"), std::string::npos);
}

TEST(ListAndCancelReminderActionTest, RoundTripsReminderLifecycle)
{
    TemporaryDirectory temporaryDirectory;
    ReminderStore store(temporaryDirectory.path() + "/reminders.db");
    ReminderService service(store);
    ListRemindersAction listAction(service);
    CancelReminderAction cancelAction(service);

    EXPECT_NE(listAction.execute({}, {10, 0, ""}).content.find("没有待触发"),
              std::string::npos);

    SetReminderAction setAction(service);
    const std::string iso = futureIsoTime();
    setAction.execute({{"content", "开会"}, {"time", iso}}, {10, 0, ""});
    const auto listed = listAction.execute({}, {10, 0, ""});
    EXPECT_NE(listed.content.find("开会"), std::string::npos);
    EXPECT_NE(listed.content.find("编号 1"), std::string::npos);

    // 其他用户看不到，也不能取消
    EXPECT_NE(listAction.execute({}, {20, 0, ""}).content.find("没有待触发"),
              std::string::npos);
    EXPECT_NE(cancelAction.execute({{"id", 1}}, {20, 0, ""}).content.find("未找到"),
              std::string::npos);
    EXPECT_NE(cancelAction.execute({{"id", 1}}, {10, 0, ""}).content.find("已取消"),
              std::string::npos);
    EXPECT_TRUE(service.list(10).empty());
    EXPECT_NE(cancelAction.execute({{"id", 1}}, {10, 0, ""}).content.find("未找到"),
              std::string::npos);
    EXPECT_NE(cancelAction.execute({}, {10, 0, ""}).content.find("错误"),
              std::string::npos);
}
