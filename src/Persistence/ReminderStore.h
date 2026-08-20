#ifndef REMINDER_STORE_H
#define REMINDER_STORE_H

#include <sqlite3.h>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// 提醒的持久化记录：trigger_at 为本地时区换算的 unix 秒
struct ReminderRecord
{
    int64_t id = 0;
    uint64_t user_id = 0;
    std::string content;
    int64_t trigger_at = 0;
    std::string repeat_rule; // none | daily | weekly
    int64_t created_at = 0;
};

// SQLite 存储适配器：提醒的写入/查询/删除/滚动触发时间。
// 类内 mutex 串行化访问；db 打开失败时降级为 no-op/空结果，均记 LOG_ERROR。
class ReminderStore
{
public:
    explicit ReminderStore(const std::string &dbPath);
    ~ReminderStore();

    ReminderStore(const ReminderStore &) = delete;
    ReminderStore &operator=(const ReminderStore &) = delete;

    bool isOpen() const { return db != nullptr; }

    // 写入一条提醒，返回自增 id（失败返回 0）
    int64_t insert(uint64_t user_id, const std::string &content,
                   int64_t trigger_at, const std::string &repeat_rule);

    // 某用户全部待触发提醒，按 trigger_at 升序
    std::vector<ReminderRecord> listForUser(uint64_t user_id);

    std::size_t countForUser(uint64_t user_id);

    // 全部待触发提醒（启动重建内存队列用），按 trigger_at 升序
    std::vector<ReminderRecord> loadPending();

    bool deleteById(int64_t id);

    // 重复提醒滚动到下一次触发时间
    bool updateTrigger(int64_t id, int64_t trigger_at);

private:
    ReminderRecord rowToRecord(sqlite3_stmt *stmt);

    sqlite3 *db = nullptr;
    mutable std::mutex dbMutex;
};

#endif // REMINDER_STORE_H
