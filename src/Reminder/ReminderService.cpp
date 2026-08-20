#include "ReminderService.h"
#include "../Log/Log.h"

ReminderService::ReminderService(ReminderStore &store) : store(store)
{
    const int64_t now = nowSeconds();
    int64_t recovered = 0;
    int64_t dropped = 0;
    for (const ReminderRecord &record : store.loadPending())
    {
        if (record.trigger_at > now)
        {
            DueEvent event;
            event.id = record.id;
            event.user_id = record.user_id;
            event.content = record.content;
            event.scheduled_at = record.trigger_at;
            event.repeat_rule = record.repeat_rule;
            event.late = false;
            enqueue(event);
            continue;
        }
        // 漏触发：重启期间错过的事件
        const bool withinWindow = now - record.trigger_at <= kLateWindowSeconds;
        if (record.repeat_rule != "none")
        {
            if (withinWindow)
            {
                // 只补发迟到事件，popDue 触发时自然滚动到下一次，
                // 避免"构造滚动 + 触发滚动"造成同一次提醒重复入队
                DueEvent missedEvent;
                missedEvent.id = record.id;
                missedEvent.user_id = record.user_id;
                missedEvent.content = record.content;
                missedEvent.scheduled_at = record.trigger_at;
                missedEvent.repeat_rule = record.repeat_rule;
                missedEvent.late = true;
                enqueue(missedEvent);
                ++recovered;
            }
            else
            {
                // 超过补发窗口：不补发错过的这一轮，直接滚动到下一次
                const int64_t next = nextOccurrenceAfter(record.trigger_at,
                                                         record.repeat_rule, now);
                store.updateTrigger(record.id, next);
                DueEvent nextEvent;
                nextEvent.id = record.id;
                nextEvent.user_id = record.user_id;
                nextEvent.content = record.content;
                nextEvent.scheduled_at = next;
                nextEvent.repeat_rule = record.repeat_rule;
                nextEvent.late = false;
                enqueue(nextEvent);
            }
            continue;
        }
        if (withinWindow)
        {
            // 24 小时内错过的一次性提醒：立即补发并标注迟到
            DueEvent event;
            event.id = record.id;
            event.user_id = record.user_id;
            event.content = record.content;
            event.scheduled_at = record.trigger_at;
            event.repeat_rule = record.repeat_rule;
            event.late = true;
            enqueue(event);
            ++recovered;
        }
        else
        {
            store.deleteById(record.id);
            ++dropped;
            LOG_WARNING("丢弃超过补发窗口的漏触发提醒：id=" +
                        std::to_string(record.id) + "，原定时间 " +
                        formatLocal(record.trigger_at));
        }
    }
    if (recovered > 0 || dropped > 0)
        LOG_INFO("提醒队列重建完成：补发 " + std::to_string(recovered) + " 条，丢弃 " +
                 std::to_string(dropped) + " 条");
}

void ReminderService::enqueue(const DueEvent &event)
{
    queue[event.scheduled_at].push_back(event);
}

int64_t ReminderService::add(uint64_t user_id, const std::string &content,
                             int64_t trigger_at, const std::string &repeat_rule)
{
    if (content.empty() ||
        (repeat_rule != "none" && repeat_rule != "daily" && repeat_rule != "weekly"))
        return 0;
    const int64_t id = store.insert(user_id, content, trigger_at, repeat_rule);
    if (id == 0)
        return 0;
    DueEvent event;
    event.id = id;
    event.user_id = user_id;
    event.content = content;
    event.scheduled_at = trigger_at;
    event.repeat_rule = repeat_rule;
    event.late = false;
    std::lock_guard<std::mutex> lock(queueMutex);
    enqueue(event);
    return id;
}

std::vector<ReminderRecord> ReminderService::list(uint64_t user_id)
{
    return store.listForUser(user_id);
}

bool ReminderService::cancel(uint64_t user_id, int64_t id)
{
    if (id <= 0)
        return false;
    // 先在队列中找到并移除，再校验归属删行，两步都在锁内保证一致
    std::lock_guard<std::mutex> lock(queueMutex);
    for (auto bucket = queue.begin(); bucket != queue.end(); ++bucket)
    {
        auto &events = bucket->second;
        for (auto entry = events.begin(); entry != events.end(); ++entry)
        {
            if (entry->id != id)
                continue;
            if (entry->user_id != user_id)
                return false;
            events.erase(entry);
            if (events.empty())
                queue.erase(bucket);
            return store.deleteById(id);
        }
    }
    // 队列中没有（理论上不应发生）：以归属校验兜底删行
    const auto pending = store.listForUser(user_id);
    for (const ReminderRecord &record : pending)
    {
        if (record.id == id)
            return store.deleteById(id);
    }
    return false;
}

std::vector<DueEvent> ReminderService::popDue(int64_t now)
{
    std::vector<DueEvent> due;
    std::lock_guard<std::mutex> lock(queueMutex);
    while (!queue.empty() && queue.begin()->first <= now)
    {
        std::vector<DueEvent> bucket = std::move(queue.begin()->second);
        queue.erase(queue.begin());
        for (DueEvent &event : bucket)
        {
            due.push_back(event);
            if (event.repeat_rule == "none")
                store.deleteById(event.id);
            else
            {
                const int64_t next = nextOccurrenceAfter(event.scheduled_at,
                                                         event.repeat_rule, now);
                store.updateTrigger(event.id, next);
                event.scheduled_at = next;
                event.late = false;
                enqueue(event);
            }
        }
    }
    return due;
}
