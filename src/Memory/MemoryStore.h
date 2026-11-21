#ifndef MEMORY_STORE_H
#define MEMORY_STORE_H

#include "MemoryItem.h"
#include "StructuredFact.h"
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <utility>
#include <vector>

class MemoryStore
{
public:
    explicit MemoryStore(const std::string &dbPath);
    ~MemoryStore();

    MemoryStore(const MemoryStore &) = delete;
    MemoryStore &operator=(const MemoryStore &) = delete;

    bool isOpen() const;
    void upsert(const MemoryItem &item);
    void deactivate(uint64_t user_id, const std::string &memoryKey);
    void deactivateBySourceFrom(uint64_t user_id, int64_t firstSourceId);
    void clearUser(uint64_t user_id);
    std::vector<MemoryItem> search(uint64_t user_id, const std::vector<std::string> &queries,
                                   std::size_t limit);
    std::vector<StructuredFactResult> searchFacts(
        uint64_t user_id, const std::vector<StructuredFactQuery> &queries, std::size_t limit);
    std::vector<StructuredFactQuery> planFactQueries(uint64_t user_id,
                                                    const std::string &text,
                                                    std::size_t limit = 5);

    // 人物卡（ADR-032）：卡片的槽位存成事实表里 subject_key='user:self'、谓词以 card. 开头的当前事实，
    // 历史、按来源回滚、按人清除都沿用事实表的；不建新表、不加列。
    // cardSlots：某人当前有效的卡片事实（谓词 → 值）；setCardSlot：写一个槽位，value 为空 = 清掉
    std::vector<std::pair<std::string, std::string>> cardSlots(uint64_t user_id);
    bool setCardSlot(uint64_t user_id, const std::string &predicate, const std::string &value,
                     const std::string &canonicalText, int64_t sourceId);

private:
    void upsertStructuredFact(const MemoryItem &item, int64_t now);
    void deactivateStructuredFact(uint64_t user_id, const std::string &memoryKey,
                                  int64_t validToSourceId, const std::string &status);
    void restoreStructuredFactsBefore(uint64_t user_id, int64_t firstSourceId);
    sqlite3 *db = nullptr;
    mutable std::mutex dbMutex;
};

#endif // MEMORY_STORE_H
