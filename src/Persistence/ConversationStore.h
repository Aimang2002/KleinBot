#ifndef CONVERSATION_STORE_H
#define CONVERSATION_STORE_H

#include "../Message/Person.hpp" // TimestampedMessage
#include <sqlite3.h>
#include <string>
#include <vector>
#include <cstdint>

// SQLite 存储适配器：对话历史的落盘/读回/检索/删除。
// 线程安全由调用方（UserSessionService 的 mutex_message）保证，本类不加锁。
// db 打开失败时所有方法降级：写入 no-op、读取返回空，均记 LOG_ERROR。
class ConversationStore
{
public:
    explicit ConversationStore(const std::string &dbPath);
    ~ConversationStore();

    ConversationStore(const ConversationStore &) = delete;
    ConversationStore &operator=(const ConversationStore &) = delete;

    bool isOpen() const { return db != nullptr; }

    // 追加一条消息（write-through 的写入点）
    void append(uint64_t user_id, const std::string &role,
                const std::string &content, time_t timestamp);

    // 读回某用户全部历史，按 id 升序（= 插入顺序）
    std::vector<TimestampedMessage> loadAll(uint64_t user_id);

    // 关键词字面检索（LIKE '%kw%'），按 id 升序
    std::vector<TimestampedMessage> search(uint64_t user_id, const std::string &keyword);

    // 删除某用户全部对话（配合 resetChat）
    void clearUser(uint64_t user_id);

    // 删除某用户末尾 count 条（配合 removePreviousContext，按 id 倒序删）
    void removeLast(uint64_t user_id, int count);

private:
    sqlite3 *db = nullptr;
};

#endif // CONVERSATION_STORE_H
