#include "PerceptionChannel.h"
#include "../Log/Log.h"
#include "../utils/TextTokenize.h"

#include <algorithm>
#include <chrono>

namespace
{
// 滑窗双阈值与容量：2 小时或最近 500 条，n-gram 容量 2000（D7/计划 T7 定值）
constexpr std::time_t kWindowSeconds = 2 * 60 * 60;
constexpr std::size_t kWindowMaxEntries = 500;
constexpr std::size_t kWindowMaxNgrams = 2000;
// flush 节流：30s 间隔或累计 delta ≥100（pollingThread 3s 轮询触发）
constexpr std::time_t kFlushIntervalSeconds = 30;
constexpr std::size_t kFlushThresholdDeltas = 100;
constexpr std::time_t kHourSeconds = 60 * 60;
} // namespace

PerceptionChannel::PerceptionChannel(GroupListService *state, PerceptionStore *store)
    : state(state), store(store)
{
}

bool PerceptionChannel::observingGroup(std::uint64_t groupId) const
{
    // 逐步查询：面板改动入库后立即生效，无需重启
    return state != nullptr && state->featureEnabled() && state->isMonitored(groupId);
}

void PerceptionChannel::observeMessage(const InboundMessage &message)
{
    observe(message, false);
}

void PerceptionChannel::observeInteraction(const InboundMessage &message)
{
    observe(message, true);
}

void PerceptionChannel::observe(const InboundMessage &message, bool interaction)
{
    if (message.message_type != "group" || !observingGroup(message.group_id))
        return;

    // 原文出栈即弃：n-gram 提取后本函数不再持有 plaintext（D7）
    const std::vector<std::string> ngrams = extractNgrams(message.plain_text);
    const std::time_t eventTime = message.message_timestamp > 0
                                      ? message.message_timestamp
                                      : std::time(nullptr);

    std::lock_guard<std::mutex> lock(mutex);
    HeatWindow &window = windows[message.group_id];
    pruneWindow(window, eventTime);
    window.entries.emplace_back(eventTime, ngrams);
    for (const std::string &gram : ngrams)
        ++window.counts[gram];
    // n-gram 容量兜底：淘汰计数最小项（触发罕见，O(n) 扫描可接受）
    if (window.counts.size() > kWindowMaxNgrams)
    {
        auto minimal = window.counts.begin();
        for (auto it = window.counts.begin(); it != window.counts.end(); ++it)
        {
            if (it->second < minimal->second)
                minimal = it;
        }
        window.counts.erase(minimal);
    }

    AffinityAggregate &aggregate = affinity[message.user_id][message.group_id];
    if (interaction)
        ++aggregate.interaction;
    else
        ++aggregate.observed;
    aggregate.lastSeen = eventTime;
    ++pendingDeltas;

    ++hourMessages;
    hourActiveGroups.insert(message.group_id);
}

void PerceptionChannel::pruneWindow(HeatWindow &window, std::time_t now) const
{
    while (!window.entries.empty())
    {
        const bool expired = window.entries.front().first + kWindowSeconds < now;
        // >= 而不是 >：插入前先腾位，稳态恰好保持 500 条
        const bool overflow = window.entries.size() >= kWindowMaxEntries;
        if (!expired && !overflow)
            break;
        for (const std::string &gram : window.entries.front().second)
        {
            const auto it = window.counts.find(gram);
            if (it == window.counts.end())
                continue;
            if (it->second <= 1)
                window.counts.erase(it);
            else
                --it->second;
        }
        window.entries.pop_front();
    }
}

std::vector<std::string> PerceptionChannel::extractNgrams(const std::string &plainText)
{
    std::vector<std::string> ngrams;
    const std::string normalized = utils::normalizeText(plainText);
    if (normalized.empty())
        return ngrams;

    for (const std::string &word : utils::splitWords(normalized))
    {
        if (utils::hasNonAscii(word))
        {
            // CJK：字符 2-gram；功能词（"我们"这类高频噪声）不进热度
            const std::vector<std::string> characters = utils::utf8Characters(word);
            for (std::size_t start = 0; start + 2 <= characters.size(); ++start)
            {
                std::string gram = characters[start] + characters[start + 1];
                if (!utils::isStopTerm(gram))
                    ngrams.push_back(std::move(gram));
            }
        }
        else if (word.size() >= 2 && !utils::isStopTerm(word))
        {
            // ASCII：整词作 term（单字母词是噪声）
            ngrams.push_back(word);
        }
    }
    return ngrams;
}

void PerceptionChannel::flushDue(std::time_t now)
{
    if (state == nullptr || !state->featureEnabled())
        return;

    // 节流判断与快照同锁：pendingDeltas 由 worker 线程递增，读必须持锁
    bool hourRollover = false;
    std::vector<AffinityDelta> deltas;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (lastHourLog == 0)
            lastHourLog = now;
        hourRollover = now - lastHourLog >= kHourSeconds;
        const bool due = pendingDeltas > 0 &&
                         (now - lastFlush >= kFlushIntervalSeconds ||
                          pendingDeltas >= kFlushThresholdDeltas);
        if (due)
        {
            deltas.reserve(affinity.size());
            for (const auto &perUser : affinity)
            {
                for (const auto &perGroup : perUser.second)
                {
                    deltas.push_back({perUser.first, perGroup.first,
                                      perGroup.second.observed,
                                      perGroup.second.interaction,
                                      perGroup.second.lastSeen});
                }
            }
            affinity.clear();
            pendingDeltas = 0;
            lastFlush = now;
        }
        else if (!hourRollover)
        {
            return; // 节流窗口外的空转：只检查整点汇总，不动库
        }
    }

    // 锁外写库：SQLite IO 不阻塞 worker 的观察入口
    if (!deltas.empty())
    {
        const auto started = std::chrono::steady_clock::now();
        if (store != nullptr && store->isOpen())
        {
            store->upsertBatch(deltas);
        }
        else
        {
            if (!warnedStoreUnavailable)
            {
                LOG_WARNING("观察通道存储不可用，亲密度计数将被丢弃（仅告警一次）");
                warnedStoreUnavailable = true;
            }
        }
        const std::chrono::duration<double, std::milli> elapsed =
            std::chrono::steady_clock::now() - started;
        std::lock_guard<std::mutex> lock(mutex);
        ++hourFlushCount;
        hourFlushMillis += elapsed.count();
    }

    if (hourRollover)
    {
        std::lock_guard<std::mutex> lock(mutex);
        LOG_INFO("观察通道小时汇总：消息 " + std::to_string(hourMessages) +
                 " 条，活跃群 " + std::to_string(hourActiveGroups.size()) + " 个，flush " +
                 std::to_string(hourFlushCount) + " 次共 " +
                 std::to_string(static_cast<long>(hourFlushMillis)) + "ms");
        hourMessages = 0;
        hourActiveGroups.clear();
        hourFlushCount = 0;
        hourFlushMillis = 0.0;
        lastHourLog = now;
    }
}

std::vector<std::pair<std::string, double>> PerceptionChannel::hotTopics(
    std::uint64_t groupId, std::size_t topN) const
{
    std::vector<std::pair<std::string, double>> topics;
    if (state == nullptr || !state->featureEnabled())
        return topics;

    std::lock_guard<std::mutex> lock(mutex);
    const auto it = windows.find(groupId);
    if (it == windows.end())
        return topics;

    topics.reserve(it->second.counts.size());
    for (const auto &entry : it->second.counts)
    {
        if (entry.second > 0)
            topics.push_back({entry.first, static_cast<double>(entry.second)});
    }
    if (topics.size() > topN)
    {
        std::partial_sort(topics.begin(), topics.begin() + static_cast<long>(topN), topics.end(),
                          [](const auto &left, const auto &right) {
                              return left.second > right.second;
                          });
        topics.resize(topN);
    }
    else
    {
        std::sort(topics.begin(), topics.end(), [](const auto &left, const auto &right) {
            return left.second > right.second;
        });
    }
    return topics;
}

std::string PerceptionChannel::topicNoteFor(std::uint64_t groupId) const
{
    // 消费端（v2.4.1）：被 @ 时给本轮回复带上群内近期热点。
    // count ≥2 才算"常聊"——只出现过一次的碎片是噪声，宁可不给
    const auto topics = hotTopics(groupId, 6);
    if (topics.empty() || topics.front().second < 2)
        return {};

    std::string fragments;
    for (const auto &topic : topics)
    {
        if (topic.second < 2)
            break; // 已按热度降序，其后都更冷
        if (!fragments.empty())
            fragments += "、";
        fragments += topic.first;
    }
    if (fragments.empty())
        return {};

    // 纯自然叙事（T6 教训：元词汇与方括号任务标记会被当成待应答的对话，
    // 这里保留 [系统注] 最小标记 + 数据本身，不写指令腔）
    return "[系统注] 这是群聊，群里最近常聊：" + fragments +
           "。回应时可自然利用这些背景，不必刻意罗列。";
}
