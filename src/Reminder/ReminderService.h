#ifndef REMINDER_SERVICE_H
#define REMINDER_SERVICE_H

#include "../Persistence/ReminderStore.h"
#include "ReminderTime.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// 到期事件，由 popDue 原子批量取出
struct DueEvent
{
    int64_t id = 0;
    uint64_t user_id = 0;
    std::string content;
    int64_t scheduled_at = 0; // 原定触发时间
    std::string repeat_rule;  // none | daily | weekly
    bool late = false;        // 重启恢复的漏触发事件
};

// 提醒调度门面：SQLite 持久化 + 内存到期队列。
// 单一 mutex 同时保护队列与排队/取消/触发的一致性；
// 构造时重建内存队列，超过补发窗口的漏触发事件直接删除。
class ReminderService
{
public:
    explicit ReminderService(ReminderStore &store);

    ReminderService(const ReminderService &) = delete;
    ReminderService &operator=(const ReminderService &) = delete;

    // 注册提醒：持久化 + 入队，返回编号（失败返回 0）。
    // 不做时间校验（由 Action 层负责），但拒绝空内容与未知重复规则。
    int64_t add(uint64_t user_id, const std::string &content,
                int64_t trigger_at, const std::string &repeat_rule);

    std::vector<ReminderRecord> list(uint64_t user_id);

    std::size_t pendingCount(uint64_t user_id) { return store.countForUser(user_id); }

    // 取消提醒，校验归属防止跨用户删除
    bool cancel(uint64_t user_id, int64_t id);

    // 批量取出全部到期事件：none 删行，daily/weekly 滚动到下一次并重新入队
    std::vector<DueEvent> popDue(int64_t now);

    static constexpr std::size_t kMaxPendingPerUser = 20;
    static constexpr int64_t kLateWindowSeconds = 86400; // 超过 24h 的漏触发丢弃

private:
    void enqueue(const DueEvent &event);

    ReminderStore &store;
    std::mutex queueMutex;
    // key = 触发时间戳，同秒多事件用 vector 承载（旧实现单值 map 会互相覆盖）
    std::map<int64_t, std::vector<DueEvent>> queue;
};

#endif // REMINDER_SERVICE_H
