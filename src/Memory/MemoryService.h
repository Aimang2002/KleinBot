#ifndef MEMORY_SERVICE_H
#define MEMORY_SERVICE_H

#include "MemoryExtractor.h"
#include "MemoryStore.h"
#include "../Persistence/ConversationStore.h"
#include "MemoryOptions.h"
#include "../utils/Live.h"
#include <atomic>
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
    // config 是组合根的活值（ADR-023）：model、batch_turns、idle_minutes、recall_limit、enabled
    // 都在使用处取快照，改了下一次操作即用新值。后台线程在记忆库可用时始终启动（稳定外壳，
    // 运行时不创建也不销毁线程）；enabled 只在入队与召回处读：关掉后不再收新的轮次、召回不查
    // 长期记忆，已入队的批次照常处理完
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
    // 记忆库可用且当前 memory.enabled 为真
    bool isEnabled() const { return storeOpen && config.get()->enabled; }

    // 人物卡（ADR-032）：卡片存在事实表里（谓词前缀 card.），读写经记忆库自己的锁。代数在卡片可能变了的
    // 时候前进（写槽位、按人清除、按来源回滚），PersonCards 的缓存据此失效。批量提取不写这个前缀
    std::vector<std::pair<std::string, std::string>> cardSlots(uint64_t user_id);
    bool setCardSlot(uint64_t user_id, const std::string &predicate, const std::string &value,
                     const std::string &canonicalText, int64_t sourceId);
    std::uint64_t cardGeneration() const { return cardGen.load(); }

private:
    struct PendingBatch
    {
        std::vector<MemoryTurn> turns;
        std::chrono::steady_clock::time_point lastUpdated;
        uint64_t epoch = 0;
    };

    std::string recallWith(const MemoryOptions &options, uint64_t user_id,
                           const std::vector<std::string> &queries, std::size_t limit,
                           const std::vector<StructuredFactQuery> &factQueries,
                           int64_t excludeConversationId);
    void workerLoop();
    void processBatch(uint64_t user_id, PendingBatch batch, const std::string &modelName);
    bool isEpochCurrent(uint64_t user_id, uint64_t epoch);

    MemoryStore store;
    ConversationStore &conversationStore;
    MemoryExtractor extractor;
    const Live<MemoryOptions> &config;
    bool storeOpen = false; // 记忆库是否可用（构造时定，决定是否起后台线程）
    std::atomic<std::uint64_t> cardGen{0}; // 人物卡代数（见 cardGeneration）

    std::thread worker;
    std::mutex queueMutex;
    std::condition_variable queueChanged;
    std::unordered_map<uint64_t, PendingBatch> pending;
    std::unordered_map<uint64_t, uint64_t> userEpoch;
    bool stopping = false;
};

#endif // MEMORY_SERVICE_H
