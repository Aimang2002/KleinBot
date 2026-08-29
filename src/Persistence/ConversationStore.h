#ifndef CONVERSATION_STORE_H
#define CONVERSATION_STORE_H

#include "../Message/Person.hpp" // TimestampedMessage
#include <sqlite3.h>
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

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
    int64_t append(uint64_t user_id, const std::string &role,
                   const std::string &content, time_t timestamp);

    // 读回某用户全部历史，按 id 升序（= 插入顺序），供召回/检索使用
    std::vector<TimestampedMessage> loadAll(uint64_t user_id);

    // 读回某用户 id >= startId 的历史，按 id 升序（冷启动加载入口）
    std::vector<TimestampedMessage> loadFrom(uint64_t user_id, int64_t startId);

    // 读取某用户上下文起点（#重置对话 轻重置的持久化边界），无记录返回 0
    int64_t contextStartId(uint64_t user_id);

    // 设置/归零上下文起点；起点只影响 loadFrom，不删除任何历史行
    void setContextStartId(uint64_t user_id, int64_t startId);

    // 读取某用户持久化的人格（#设置人格 写入），无记录返回空串
    std::string loadPersona(uint64_t user_id);

    // 写入/覆盖人格；clearUser 与 #重置上下文 不触碰人格行
    void savePersona(uint64_t user_id, const std::string &prompt);

    // 删除人格记录（#人格还原 回退到 soul.md 默认人格）
    void clearPersona(uint64_t user_id);

    // 单关键词兼容接口，内部使用多查询检索，结果按 id 升序。
    std::vector<TimestampedMessage> search(uint64_t user_id, const std::string &keyword);

    // 多查询混合检索：拆分中文短语、去重并按文本相关性排序。
    std::vector<TimestampedMessage> searchMany(uint64_t user_id,
                                               const std::vector<std::string> &queries,
                                               std::size_t limit,
                                               int64_t excludeMessageId = 0);

    std::vector<TimestampedMessage> loadByIdRange(uint64_t user_id, int64_t start_id,
                                                  int64_t end_id, int padding = 1);

    // 删除某用户全部对话（配合 resetChat）
    void clearUser(uint64_t user_id);

    // 删除某用户末尾 count 条（配合 removePreviousContext，按 id 倒序删）
    int64_t removeLast(uint64_t user_id, int count);

private:
    sqlite3 *db = nullptr;
    mutable std::mutex dbMutex;
};

#endif // CONVERSATION_STORE_H
