#ifndef MEMORY_SERVICE_H
#define MEMORY_SERVICE_H

#include "MemoryExtractor.h"
#include "MemoryStore.h"
#include "../Persistence/ConversationStore.h"
#include "MemoryOptions.h"
#include "../utils/Live.h"
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
    // config 是组合根的活值（ADR-023）：model、batch_turns、idle_minutes、recall_limit 在使用处
    // 取快照，改了下一次操作即用新值。enabled 只在构造时读一次，决定是否启动后台线程
    // （运行中不跟随重载，L2.b 再改）
    MemoryService(const std::string &dbPath, ConversationStore &conversationStore,
                  Dock &dock, const ModelRegistry &models, const Live<MemoryOptions> &config);
    ~MemoryService();

    MemoryService(const MemoryService &) = delete;
    MemoryService &operator=(const MemoryService &) = delete;

    void enqueueTurn(uint64_t user_id, const std::string &userText,
                     const std::string &assistantText, int64_t sourceStartId,
                     int64_t sourceEndId);
    std::string recall(uint64_t user_id, const std::vector<std::string> &queries,
                       std::size_t limit,
                       const std::vector<StructuredFactQuery> &factQueries = {},
                       int64_t excludeConversationId = 0);
    std::string recallForMessage(uint64_t user_id, const std::string &text,
                                 int64_t currentMessageId = 0);
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
    void processBatch(uint64_t user_id, PendingBatch batch, const std::string &modelName);
    bool isEpochCurrent(uint64_t user_id, uint64_t epoch);

    MemoryStore store;
    ConversationStore &conversationStore;
    MemoryExtractor extractor;
    const Live<MemoryOptions> &config;
    bool enabled = true; // 启动值：记忆库可用且启动时 memory.enabled 为真

    std::thread worker;
    std::mutex queueMutex;
    std::condition_variable queueChanged;
    std::unordered_map<uint64_t, PendingBatch> pending;
    std::unordered_map<uint64_t, uint64_t> userEpoch;
    bool stopping = false;
};

#endif // MEMORY_SERVICE_H
