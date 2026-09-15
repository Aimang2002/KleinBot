#ifndef TOPIC_TRACKER_H
#define TOPIC_TRACKER_H

/*
 * 话题生命周期状态机（T7b，骨架版意志层）：每群最多 3 个 TopicSlot
 * （用户定规），权重纯数学零 LLM（词汇漂移采样，复用 scoreRecallText），
 * LLM 只花在评估点上（lull 触发的"要不要说话"）。线程模型：onMessage*
 * 在 worker 线程、pump 在 pollingThread、prepareEvaluation/consumeEvaluation
 * 在评估 worker——内部 mutex 统一保护。
 *
 * 状态：ENGAGED（在跟，lull 评估）→ 连续拒绝/无人接话 → TRACKING
 * （只看不说，权重继续衰减）→ 权重 < 关闭线 → slot 释放（群回 IDLE）。
 * @/提名字是显式召唤：匹配 anchor 入槽，满槽逐出最弱；冷启动 join
 * 只用空槽。重启即失（短时状态，24h 内容库才是持久层）。
 */
#include "../Memory/TextRecall.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

enum class SlotState
{
    Engaged,  // 在跟：lull 到期触发评估
    Tracking, // 只看不说：权重衰减到关闭线
};

struct TopicSlot
{
    SlotState state = SlotState::Engaged;
    double weight = 70.0;
    RecallQueryPlan anchor;
    std::vector<std::int64_t> memberSeqs; // 话题相关消息 seq 窗口（时间序）
    std::int64_t lastRelevantTs = 0;
    std::int64_t lastSpokeTs = 0;
    std::int64_t lastEvaluatedTs = 0;
    std::int64_t openedTs = 0;
    int consecutiveRejections = 0;
    int unansweredSelfSpeeches = 0; // 她连续主动发言无人回应（人际直觉保险丝）
    bool pendingBurst = false;      // 上次评估后有新相关消息（lull 判据）
    std::string openingTrigger;
};

class TopicTracker
{
public:
    explicit TopicTracker(std::function<std::int64_t()> clock);

    // worker：新群消息 → 各 slot 权重/成员更新。返回该消息是否构成
    // join 信号（提名字无@的显式召唤，由 service 执行 join 评估）
    bool onMessage(std::uint64_t groupId, const std::string &text, bool mentionsBot,
                   bool isSelf, std::int64_t seq, std::int64_t ts,
                   const std::string &messageId, const std::string &replyToMessageId);

    // @ 到达：匹配/开槽/逐出最弱，置 ENGAGED 并刷新 anchor
    void onAtTriggered(std::uint64_t groupId, const std::string &triggerText,
                       const std::vector<std::string> &contextTexts);

    // 她发言（成功送达的群出站，@ 回应与主动发言都算）
    void onSelfSpoke(std::uint64_t groupId, std::int64_t now);

    // [不回应]：权重惩罚 + 连续拒绝计数（达限转 TRACKING）
    void onSuppressed(std::uint64_t groupId, std::int64_t now);

    // pollingThread：时间衰减 + lull/max-wait/礼貌窗/保险丝 → 到期群
    std::vector<std::uint64_t> pump(std::int64_t now);

    // 评估材料：该群当前最该评估的 slot 的成员消息 prompt 头部
    // （时间序原文行）；返回空串表示没有待评估话题
    std::string prepareEvaluation(std::uint64_t groupId, std::int64_t now);

    // join 评估的 fuse：每小时每群 ≤1 次；true = 放行
    bool allowJoinEvaluation(std::uint64_t groupId, std::int64_t now);

    // 调试/测试观测
    std::vector<TopicSlot> slotsOf(std::uint64_t groupId) const;

private:
    TopicSlot *matchSlotUnderLock(std::uint64_t groupId, const std::string &text);
    void closeSlotIfExpired(std::uint64_t groupId, std::size_t index);

    const std::function<std::int64_t()> clock;
    mutable std::mutex mutex;
    std::map<std::uint64_t, std::vector<TopicSlot>> slotsPerGroup;
    std::map<std::uint64_t, std::deque<std::int64_t>> joinEvalHistory; // fuse 记录
    std::int64_t lastPumpMinuteMark = 0;
};

#endif // TOPIC_TRACKER_H
