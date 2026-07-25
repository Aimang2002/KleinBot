#include "MemoryService.h"
#include "../Log/Log.h"
#include <algorithm>
#include <cctype>
#include <sstream>


MemoryService::MemoryService(const std::string &dbPath, ConversationStore &conversationStore,
                             Dock &dock, const ModelRegistry &models, const MemoryConfig &config)
    : store(dbPath), conversationStore(conversationStore), extractor(dock, models, config.model)
{
    enabled = config.enabled && store.isOpen();
    batchTurns = config.batchTurns;
    recallLimit = config.recallLimit;
    idleDelay = std::chrono::seconds(config.idleSeconds);
    if (enabled)
    {
        worker = std::thread(&MemoryService::workerLoop, this);
        LOG_INFO("长期记忆服务已启动");
    }
    else
    {
        LOG_WARNING("长期记忆服务未启用");
    }
}

MemoryService::~MemoryService()
{
    shutdown();
}

void MemoryService::enqueueTurn(uint64_t user_id, const std::string &userText,
                                const std::string &assistantText, int64_t sourceStartId,
                                int64_t sourceEndId)
{
    if (!enabled || userText.empty() || assistantText.empty())
        return;

    std::lock_guard<std::mutex> lock(queueMutex);
    if (stopping)
        return;
    PendingBatch &batch = pending[user_id];
    batch.epoch = userEpoch[user_id];
    batch.lastUpdated = std::chrono::steady_clock::now();
    batch.turns.push_back({userText, assistantText, sourceStartId, sourceEndId});
    queueChanged.notify_one();
}

std::string MemoryService::recall(uint64_t user_id, const std::vector<std::string> &queries,
                                  std::size_t limit)
{
    const std::size_t effectiveLimit = limit == 0 ? recallLimit : std::min(limit, recallLimit);
    std::ostringstream output;
    if (enabled)
    {
        auto memories = store.search(user_id, queries, effectiveLimit);
        if (!memories.empty())
        {
            output << "长期记忆命中：\n";
            for (const auto &memory : memories)
            {
                output << "- [" << memory.memory_type << "] " << memory.canonical_text << "\n";
                auto evidence = conversationStore.loadByIdRange(
                    user_id, memory.source_start_id, memory.source_end_id, 1);
                if (!evidence.empty())
                {
                    output << "  原始对话证据：\n";
                    for (const auto &message : evidence)
                        output << "  [" << message.role << "] " << message.content << "\n";
                }
            }
            return output.str();
        }
    }

    bool foundRawConversation = false;
    output << "长期记忆未命中，原始历史检索结果：\n";
    std::size_t remaining = effectiveLimit;
    for (const auto &query : queries)
    {
        if (query.empty() || remaining == 0)
            continue;
        auto messages = conversationStore.search(user_id, query);
        for (const auto &message : messages)
        {
            output << "[" << message.role << "] " << message.content << "\n";
            foundRawConversation = true;
            if (--remaining == 0)
                break;
        }
    }
    if (!foundRawConversation)
        return "没有找到与这些检索词相关的长期记忆或原始对话。";
    return output.str();
}

void MemoryService::clearUser(uint64_t user_id)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        ++userEpoch[user_id];
        pending.erase(user_id);
    }
    store.clearUser(user_id);
}

void MemoryService::removeBySourceFrom(uint64_t user_id, int64_t firstSourceId)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        ++userEpoch[user_id];
        pending.erase(user_id);
    }
    store.deactivateBySourceFrom(user_id, firstSourceId);
}

void MemoryService::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (stopping)
            return;
        stopping = true;
        pending.clear();
    }
    queueChanged.notify_all();
    if (worker.joinable())
        worker.join();
}

void MemoryService::workerLoop()
{
    while (true)
    {
        uint64_t selectedUser = 0;
        PendingBatch selectedBatch;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueChanged.wait_for(lock, std::chrono::milliseconds(500));
            if (stopping)
                return;

            const auto now = std::chrono::steady_clock::now();
            for (auto iterator = pending.begin(); iterator != pending.end(); ++iterator)
            {
                const bool batchReady = iterator->second.turns.size() >= batchTurns;
                const bool idleReady = now - iterator->second.lastUpdated >= idleDelay;
                if (!batchReady && !idleReady)
                    continue;
                selectedUser = iterator->first;
                selectedBatch = std::move(iterator->second);
                pending.erase(iterator);
                break;
            }
        }

        if (selectedUser != 0 && !selectedBatch.turns.empty())
            processBatch(selectedUser, std::move(selectedBatch));
    }
}

void MemoryService::processBatch(uint64_t user_id, PendingBatch batch)
{
    auto mutations = extractor.extract(user_id, batch.turns);
    if (!isEpochCurrent(user_id, batch.epoch))
        return;
    for (const auto &mutation : mutations)
    {
        if (!isEpochCurrent(user_id, batch.epoch))
            return;
        if (mutation.action == "delete")
            store.deactivate(user_id, mutation.item.memory_key);
        else
            store.upsert(mutation.item);
    }
    if (!mutations.empty())
        LOG_INFO("长期记忆已更新，用户：" + std::to_string(user_id) +
                 "，条目数：" + std::to_string(mutations.size()));
}

bool MemoryService::isEpochCurrent(uint64_t user_id, uint64_t epoch)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    return userEpoch[user_id] == epoch && !stopping;
}
