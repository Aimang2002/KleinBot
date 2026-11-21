#include "MemoryService.h"
#include "PersonCard.h"
#include "../Log/Log.h"
#include "MemoryQueryPlanner.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{
enum class RecallKind
{
    StructuredFact,
    Memory,
    RawConversation
};

struct RankedRecall
{
    RecallKind kind = RecallKind::RawConversation;
    std::size_t index = 0;
    double relevance = 0.0;
    int64_t recency = 0;
};

bool coveredByMemory(const TimestampedMessage &message,
                     const std::vector<MemoryItem> &memories,
                     double minimumRelevance)
{
    if (message.id <= 0)
        return false;
    for (const auto &memory : memories)
    {
        if (memory.relevance < minimumRelevance)
            continue;
        if (message.id >= memory.source_start_id && message.id <= memory.source_end_id)
            return true;
    }
    return false;
}

bool coveredByStructuredFact(const TimestampedMessage &message,
                             const std::vector<StructuredFactResult> &facts,
                             double minimumRelevance)
{
    if (message.id <= 0)
        return false;
    for (const auto &fact : facts)
    {
        if (fact.relevance + 40.0 < minimumRelevance)
            continue;
        if (message.id >= fact.source_start_id && message.id <= fact.source_end_id)
            return true;
    }
    return false;
}
}

MemoryService::MemoryService(const std::string &dbPath, ConversationStore &conversationStore,
                             Dock &dock, const ModelRegistry &models, const Live<MemoryOptions> &config)
    : store(dbPath), conversationStore(conversationStore), extractor(dock, models), config(config)
{
    storeOpen = store.isOpen();
    if (storeOpen)
    {
        worker = std::thread(&MemoryService::workerLoop, this);
        if (config.get()->enabled)
            LOG_INFO("长期记忆服务已启动");
        else
            LOG_WARNING("长期记忆服务未启用（memory.enabled=false；后台线程常驻，打开后即生效）");
    }
    else
    {
        LOG_WARNING("长期记忆服务未启用：记忆库不可用");
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
    // 开关在队列锁之外取（活值的锁不与 queueMutex 嵌套）
    if (!storeOpen || !config.get()->enabled || userText.empty() || assistantText.empty())
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
                                  std::size_t limit,
                                  const std::vector<StructuredFactQuery> &factQueries,
                                  int64_t excludeConversationId)
{
    // 一次召回取一次配置快照
    const Live<MemoryOptions>::Snapshot options = config.get();
    return recallWith(*options, user_id, queries, limit, factQueries, excludeConversationId);
}

std::string MemoryService::recallWith(const MemoryOptions &options, uint64_t user_id,
                                      const std::vector<std::string> &queries, std::size_t limit,
                                      const std::vector<StructuredFactQuery> &factQueries,
                                      int64_t excludeConversationId)
{
    const std::size_t recallLimit = options.recallLimit;
    const std::size_t effectiveLimit = limit == 0 ? recallLimit : std::min(limit, recallLimit);
    const std::size_t candidateLimit = effectiveLimit * 3;
    std::vector<MemoryItem> memories;
    std::vector<StructuredFactResult> facts;
    // 长期记忆关着时只查原始对话（与启动时关闭的行为相同）
    if (storeOpen && options.enabled)
    {
        facts = store.searchFacts(user_id, factQueries, candidateLimit);
        memories = store.search(user_id, queries, candidateLimit);
    }
    auto rawMessages = conversationStore.searchMany(
        user_id, queries, candidateLimit, excludeConversationId);

    std::vector<RankedRecall> ranked;
    ranked.reserve(facts.size() + memories.size() + rawMessages.size());
    for (std::size_t index = 0; index < facts.size(); ++index)
        ranked.push_back({RecallKind::StructuredFact, index, facts[index].relevance + 40.0,
                          facts[index].source_start_id});
    for (std::size_t index = 0; index < memories.size(); ++index)
    {
        bool duplicatesFact = false;
        for (const auto &fact : facts)
        {
            if (memories[index].source_start_id == fact.source_start_id &&
                memories[index].source_end_id == fact.source_end_id &&
                memories[index].canonical_text == fact.canonical_text)
            {
                duplicatesFact = true;
                break;
            }
        }
        if (!duplicatesFact)
            ranked.push_back({RecallKind::Memory, index, memories[index].relevance,
                              memories[index].updated_at});
    }
    for (std::size_t index = 0; index < rawMessages.size(); ++index)
    {
        if (coveredByStructuredFact(rawMessages[index], facts, rawMessages[index].relevance))
            continue;
        if (coveredByMemory(rawMessages[index], memories, rawMessages[index].relevance))
            continue;
        ranked.push_back({RecallKind::RawConversation, index, rawMessages[index].relevance,
                          static_cast<int64_t>(rawMessages[index].timestamp)});
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedRecall &left,
                                                const RankedRecall &right) {
        if (left.relevance != right.relevance)
            return left.relevance > right.relevance;
        if (left.kind != right.kind)
            return static_cast<int>(left.kind) < static_cast<int>(right.kind);
        return left.recency > right.recency;
    });
    if (ranked.size() > effectiveLimit)
        ranked.resize(effectiveLimit);

    if (ranked.empty())
        return "没有找到与这些检索词相关的长期记忆或原始对话。";

    std::ostringstream output;
    output << "按相关性排序的记忆与原始历史：\n";
    for (const auto &entry : ranked)
    {
        if (entry.kind == RecallKind::RawConversation)
        {
            const auto &message = rawMessages[entry.index];
            output << "- [原始对话/" << message.role << "] " << message.content << "\n";
            continue;
        }

        if (entry.kind == RecallKind::StructuredFact)
        {
            const auto &fact = facts[entry.index];
            output << "- [结构化事实/" << fact.temporal << "] "
                   << fact.subject_name << " (" << fact.subject_key << ")."
                   << fact.predicate << " = " << fact.value_text;
            if (fact.temporal != "current")
            {
                output << " [来源区间 " << fact.valid_from_source_id;
                if (fact.valid_to_source_id > 0)
                    output << " - " << fact.valid_to_source_id;
                output << "]";
            }
            output << "\n";
            auto evidence = conversationStore.loadByIdRange(
                user_id, fact.source_start_id, fact.source_end_id, 1);
            if (!evidence.empty())
            {
                output << "  原始对话证据：\n";
                for (const auto &message : evidence)
                {
                    if (message.id == excludeConversationId)
                        continue;
                    output << "  [" << message.role << "] " << message.content << "\n";
                }
            }
            continue;
        }

        const auto &memory = memories[entry.index];
        output << "- [长期记忆/" << memory.memory_type << "] "
               << memory.canonical_text << "\n";
        auto evidence = conversationStore.loadByIdRange(
            user_id, memory.source_start_id, memory.source_end_id, 1);
        if (!evidence.empty())
        {
            output << "  原始对话证据：\n";
            for (const auto &message : evidence)
            {
                if (message.id == excludeConversationId)
                    continue;
                output << "  [" << message.role << "] " << message.content << "\n";
            }
        }
    }
    return output.str();
}

std::string MemoryService::recallForMessage(uint64_t user_id, const std::string &text,
                                            int64_t currentMessageId)
{
    if (text.empty())
        return {};
    const bool explicitRecall = hasExplicitRecallIntent(text);
    const bool factQuestion = looksLikeFactQuestion(text);
    if (!explicitRecall && !factQuestion)
        return {};

    // 一次召回取一次配置快照：开关与 recall_limit 出自同一份
    const Live<MemoryOptions>::Snapshot options = config.get();
    std::vector<StructuredFactQuery> factQueries;
    if (storeOpen && options->enabled)
        factQueries = store.planFactQueries(user_id, text, 5);
    if (!explicitRecall && factQueries.empty())
        return {};

    // limit 传 0 = 按配置的 recall_limit
    const std::string result = recallWith(
        *options, user_id, {text}, 0, factQueries, currentMessageId);
    if (result == "没有找到与这些检索词相关的长期记忆或原始对话。")
        return {};
    return result;
}

void MemoryService::clearUser(uint64_t user_id)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        ++userEpoch[user_id];
        pending.erase(user_id);
    }
    store.clearUser(user_id);
    ++cardGen; // 这个人的卡片一起清掉了
}

void MemoryService::removeBySourceFrom(uint64_t user_id, int64_t firstSourceId)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        ++userEpoch[user_id];
        pending.erase(user_id);
    }
    store.deactivateBySourceFrom(user_id, firstSourceId);
    ++cardGen; // 按来源回滚可能撤回了卡片上的改动
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
        // 每一轮（挑一个批次并提取）开头取一次配置快照，在队列锁之外取：活值的锁不与
        // queueMutex 嵌套。本轮挑批次用的 batch_turns、idle_minutes 与提取用的 model 都出自
        // 这一份；配置换了，最迟下一轮（约 500 毫秒）起用新值，正在提取的批次不受影响
        const Live<MemoryOptions>::Snapshot options = config.get();
        const std::chrono::minutes idleDelay(options->idleMinutes);
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
                const bool batchReady = iterator->second.turns.size() >= options->batchTurns;
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
            processBatch(selectedUser, std::move(selectedBatch), options->model);
    }
}

void MemoryService::processBatch(uint64_t user_id, PendingBatch batch, const std::string &modelName)
{
    auto mutations = extractor.extract(user_id, batch.turns, modelName);
    if (!isEpochCurrent(user_id, batch.epoch))
        return;
    for (const auto &mutation : mutations)
    {
        if (!isEpochCurrent(user_id, batch.epoch))
            return;
        // 人物卡的谓词是保留的（ADR-032）：卡片只由 update_person_card 与程序写，批量提取不碰
        if (isPersonCardPredicate(mutation.item.predicate) ||
            isPersonCardPredicate(mutation.item.memory_key))
            continue;
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

std::vector<std::pair<std::string, std::string>> MemoryService::cardSlots(uint64_t user_id)
{
    if (!storeOpen)
        return {};
    return store.cardSlots(user_id);
}

bool MemoryService::setCardSlot(uint64_t user_id, const std::string &predicate, const std::string &value,
                                const std::string &canonicalText, int64_t sourceId)
{
    if (!storeOpen)
        return false;
    const bool ok = store.setCardSlot(user_id, predicate, value, canonicalText, sourceId);
    ++cardGen;
    return ok;
}
