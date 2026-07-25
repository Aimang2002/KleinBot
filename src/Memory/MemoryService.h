#ifndef MEMORY_SERVICE_H
#define MEMORY_SERVICE_H

#include "MemoryExtractor.h"
#include "MemoryStore.h"
#include "../Persistence/ConversationStore.h"
#include "../Configuration/AppConfig.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class MemoryService
{
public:
    MemoryService(const std::string &dbPath, ConversationStore &conversationStore,
                  Dock &dock, const ModelRegistry &models, const MemoryConfig &config);
    ~MemoryService();

    MemoryService(const MemoryService &) = delete;
    MemoryService &operator=(const MemoryService &) = delete;

    void enqueueTurn(uint64_t user_id, const std::string &userText,
                     const std::string &assistantText, int64_t sourceStartId,
                     int64_t sourceEndId);
    std::string recall(uint64_t user_id, const std::vector<std::string> &queries,
                       std::size_t limit);
    void clearUser(uint64_t user_id);
    void removeBySourceFrom(uint64_t user_id, int64_t firstSourceId);
    void shutdown();
    bool isEnabled() const { return enabled; }

private:
    struct PendingBatch
    {
        std::vector<MemoryTurn> turns;
        std::chrono::steady_clock::time_point lastUpdated;
        uint64_t epoch = 0;
    };

    void workerLoop();
    void processBatch(uint64_t user_id, PendingBatch batch);
    bool isEpochCurrent(uint64_t user_id, uint64_t epoch);

    MemoryStore store;
    ConversationStore &conversationStore;
    MemoryExtractor extractor;
    bool enabled = true;
    std::size_t batchTurns = 3;
    std::size_t recallLimit = 8;
    std::chrono::seconds idleDelay{20};

    std::thread worker;
    std::mutex queueMutex;
    std::condition_variable queueChanged;
    std::unordered_map<uint64_t, PendingBatch> pending;
    std::unordered_map<uint64_t, uint64_t> userEpoch;
    bool stopping = false;
};

#endif // MEMORY_SERVICE_H
