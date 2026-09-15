#ifndef PERCEPTION_CHANNEL_H
#define PERCEPTION_CHANNEL_H

/*
 * 观察通道（v2.4.1 T7）：只看不说、0 LLM。白名单群的消息进两条纯数据链路——
 * topic_heat（内存 n-gram 滑窗热度，重启丢失可接受）与 affinity（计数聚合，
 * 批量 UPSERT 落库）。D7 红线：消息原文只在 observe 栈内短暂存在，
 * 任何成员结构、任何落库字段都不保留原文。
 * observe* 在 worker 线程调用、flushDue 在 pollingThread 调用，内部 mutex 保护。
 */
#include "GroupListService.h"
#include "PerceptionStore.h"
#include "../Port/InboundMessage.h"

#include <cstdint>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class PerceptionChannel
{
public:
    // state：观察状态（总开关 + 监控群集）唯一真值来源，逐步查询以支持即时生效；
    // store 允许为空：无库时所有入口一律 no-op
    PerceptionChannel(GroupListService *state, PerceptionStore *store = nullptr);

    // 非 @ 群消息（workingThread 过滤失败路径）
    void observeMessage(const InboundMessage &message);
    // @bot 已处理消息（过滤通过路径）
    void observeInteraction(const InboundMessage &message);
    // pollingThread 每 3s 轮到；实际 30s 间隔或累计 delta≥100 才写库
    void flushDue(std::time_t now);
    // 该群当前热度 topN 的 (n-gram, count)：topicNoteFor 的数据源，
    // v2.4.2 心境/接话继续直接消费
    std::vector<std::pair<std::string, double>> hotTopics(std::uint64_t groupId,
                                                          std::size_t topN) const;
    // 消费端：被 @ 的白名单群生成本轮回复的话题背景注记（附在用户消息尾部）。
    // 关闭/非白名单/无 count≥2 的热点时返回空串
    std::string topicNoteFor(std::uint64_t groupId) const;

private:
    // 该群此刻是否在观察（总开关 + 监控集，实时查询以便即时生效）
    bool observingGroup(std::uint64_t groupId) const;
    struct HeatWindow
    {
        // 每条消息一个 (事件时间, 该消息贡献的 n-gram)；淘汰时按 count 递减
        std::deque<std::pair<std::time_t, std::vector<std::string>>> entries;
        std::unordered_map<std::string, std::size_t> counts;
    };

    struct AffinityAggregate
    {
        long observed = 0;
        long interaction = 0;
        std::int64_t lastSeen = 0;
    };

    void observe(const InboundMessage &message, bool interaction);
    void pruneWindow(HeatWindow &window, std::time_t now) const;
    static std::vector<std::string> extractNgrams(const std::string &plainText);

    GroupListService *const state;
    PerceptionStore *const store;

    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, HeatWindow> windows;
    std::unordered_map<std::uint64_t, std::unordered_map<std::uint64_t, AffinityAggregate>>
        affinity;
    std::size_t pendingDeltas = 0;
    std::time_t lastFlush = 0;
    bool warnedStoreUnavailable = false;

    // 成本观测（验收标准 5 的量化输出）：整点汇总一行 LOG_INFO 后清零
    std::size_t hourMessages = 0;
    std::unordered_set<std::uint64_t> hourActiveGroups;
    std::time_t lastHourLog = 0;
    long hourFlushCount = 0;
    double hourFlushMillis = 0.0;
};

#endif // PERCEPTION_CHANNEL_H
