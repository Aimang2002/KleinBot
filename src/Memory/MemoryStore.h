#ifndef MEMORY_STORE_H
#define MEMORY_STORE_H

#include "MemoryItem.h"
#include <mutex>
#include <sqlite3.h>
#include <string>
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

private:
    sqlite3 *db = nullptr;
    mutable std::mutex dbMutex;
};

#endif // MEMORY_STORE_H
