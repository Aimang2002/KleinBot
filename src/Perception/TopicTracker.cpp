#include "TopicTracker.h"

#include <algorithm>

namespace
{
// 状态机常量（真机再调的初值，零旋钮；迭代计划 T7b 定稿值）
constexpr std::size_t kMaxSlotsPerGroup = 3;
constexpr double kWeightOpen = 70.0;
constexpr double kWeightClose = 20.0;
constexpr double kRelevantFloor = 4.0;   // scoreRecallText 门槛：相关/漂移分界
constexpr double kRelevantBoostMax = 6.0;
constexpr double kDriftPenalty = 3.0;
constexpr double kPerMinuteDecay = 2.0;
constexpr double kSelfSpeakBoost = 15.0;
constexpr double kSuppressionPenalty = 10.0;
constexpr int kRejectionsToSilent = 3;   // 连续拒绝 → TRACKING
constexpr int kMaxUnansweredSpeeches = 2; // 无人接话的连续主动发言上限
constexpr std::int64_t kLullSeconds = 10; // 相关突发后静默多久算"这拍说完了"
constexpr std::int64_t kMaxWaitSeconds = 60; // 突发挂起的最长评估等待
constexpr std::int64_t kSpeechCooldown = 30; // 她发言后的礼貌静默窗
constexpr std::int64_t kGroupSpeechInterval = 45; // 跨 slot 的群级发言节流
constexpr std::int64_t kJoinFuseWindow = 60 * 60; // join 评估：每群每小时
constexpr std::size_t kAnchorWindow = 20;          // anchor 重建取最近 N 条相关消息
} // namespace

TopicTracker::TopicTracker(std::function<std::int64_t()> clock) : clock(std::move(clock))
{
}

bool TopicTracker::onMessage(std::uint64_t groupId, const std::string &text, bool mentionsBot,
                             bool isSelf, std::int64_t seq, std::int64_t ts,
                             const std::string &messageId, const std::string &replyToMessageId)
{
    (void)messageId;
    (void)replyToMessageId;

    bool joinSignal = false;
    std::lock_guard<std::mutex> lock(mutex);
    auto &groupSlots = slotsPerGroup[groupId];

    // 提名字无@（@ 走 onAtTriggered）：显式召唤的 join 信号，且无在跟槽位
    // 时才值得评估——已在跟则并入现有流程
    if (mentionsBot && !isSelf)
    {
        bool anyEngaged = false;
        for (const TopicSlot &slot : groupSlots)
        {
            if (slot.state == SlotState::Engaged)
                anyEngaged = true;
        }
        if (!anyEngaged)
            joinSignal = true;
    }

    for (TopicSlot &slot : groupSlots)
    {
        const double relevance = scoreRecallText(slot.anchor, text);
        if (relevance >= kRelevantFloor)
        {
            slot.weight = std::min(100.0, slot.weight +
                                             kRelevantBoostMax * std::min(1.0, relevance / 10.0));
            slot.lastRelevantTs = ts;
            // 突发开始时刻 = 评估等待起点（overdue 兜底的基准）；
            // 突发持续期间不重置，长突发由 lull 收口
            if (!slot.pendingBurst)
            {
                slot.pendingBurst = true;
                slot.lastEvaluatedTs = ts;
            }
            if (slot.memberSeqs.empty() || slot.memberSeqs.back() < seq)
                slot.memberSeqs.push_back(seq);
            if (slot.memberSeqs.size() > 64)
                slot.memberSeqs.erase(slot.memberSeqs.begin());
        }
        else
        {
            slot.weight = std::max(0.0, slot.weight - kDriftPenalty);
        }
    }

    // 有人接她的话（相关消息到达）清零"无人接话"计数
    for (TopicSlot &slot : groupSlots)
    {
        if (scoreRecallText(slot.anchor, text) >= kRelevantFloor)
            slot.unansweredSelfSpeeches = 0;
    }

    for (std::size_t index = 0; index < groupSlots.size();)
    {
        if (groupSlots[index].weight < kWeightClose)
        {
            groupSlots.erase(groupSlots.begin() + static_cast<long>(index));
            continue;
        }
        ++index;
    }
    return joinSignal;
}

TopicSlot *TopicTracker::matchSlotUnderLock(std::uint64_t groupId, const std::string &text)
{
    auto &groupSlots = slotsPerGroup[groupId];
    TopicSlot *best = nullptr;
    double bestScore = 0.0;
    for (TopicSlot &slot : groupSlots)
    {
        const double score = scoreRecallText(slot.anchor, text);
        if (score >= kRelevantFloor && score > bestScore)
        {
            best = &slot;
            bestScore = score;
        }
    }
    return best;
}

void TopicTracker::onAtTriggered(std::uint64_t groupId, const std::string &triggerText,
                                 const std::vector<std::string> &contextTexts)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto &groupSlots = slotsPerGroup[groupId];
    const std::int64_t now = clock();

    if (TopicSlot *matched = matchSlotUnderLock(groupId, triggerText))
    {
        // 既有话题被点名：回 ENGAGED，权重回升
        matched->state = SlotState::Engaged;
        matched->weight = std::min(100.0, matched->weight + 10.0);
        matched->lastRelevantTs = now;
        matched->pendingBurst = false; // 本轮 @ 走主回复路径，不算 lull 突发
        return;
    }

    if (groupSlots.size() >= kMaxSlotsPerGroup)
    {
        // 满槽：@ 是人类显式召唤，逐出权重最低的槽
        auto weakest = std::min_element(groupSlots.begin(), groupSlots.end(),
                                        [](const TopicSlot &left, const TopicSlot &right) {
                                            return left.weight < right.weight;
                                        });
        groupSlots.erase(weakest);
    }

    TopicSlot slot;
    slot.state = SlotState::Engaged;
    slot.weight = kWeightOpen;
    slot.openedTs = now;
    slot.lastRelevantTs = now;
    slot.lastEvaluatedTs = now; // @ 轮走主回复路径，评估等待从此刻起算
    slot.openingTrigger = triggerText;

    std::vector<std::string> queries;
    queries.push_back(triggerText);
    for (const std::string &contextText : contextTexts)
        queries.push_back(contextText);
    slot.anchor = buildRecallQueryPlan(queries, 24);
    groupSlots.push_back(std::move(slot));
}

void TopicTracker::onSelfSpoke(std::uint64_t groupId, std::int64_t now)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto &groupSlots = slotsPerGroup[groupId];
    for (TopicSlot &slot : groupSlots)
    {
        if (slot.state != SlotState::Engaged)
            continue;
        // 归属：最近的 ENGAGED 槽（同群多 ENGAGED 时按 lastRelevant 新近度）
        slot.weight = std::min(100.0, slot.weight + kSelfSpeakBoost);
        slot.lastSpokeTs = now;
        slot.lastEvaluatedTs = now;
        slot.consecutiveRejections = 0;
        slot.pendingBurst = false;
        slot.unansweredSelfSpeeches += 1;
        break;
    }
}

void TopicTracker::onSuppressed(std::uint64_t groupId, std::int64_t now)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto &groupSlots = slotsPerGroup[groupId];
    for (TopicSlot &slot : groupSlots)
    {
        if (slot.state != SlotState::Engaged)
            continue;
        slot.weight = std::max(0.0, slot.weight - kSuppressionPenalty);
        slot.lastEvaluatedTs = now;
        slot.pendingBurst = false;
        if (++slot.consecutiveRejections >= kRejectionsToSilent)
            slot.state = SlotState::Tracking;
        break;
    }
}

std::vector<std::uint64_t> TopicTracker::pump(std::int64_t now)
{
    std::vector<std::uint64_t> due;
    std::lock_guard<std::mutex> lock(mutex);

    // 每分钟一次的时间衰减（pump 3s 一跳，按分钟标记去重）
    const std::int64_t minuteMark = now / 60;
    if (minuteMark != lastPumpMinuteMark)
    {
        lastPumpMinuteMark = minuteMark;
        for (auto &[groupId, groupSlots] : slotsPerGroup)
        {
            for (TopicSlot &slot : groupSlots)
            {
                if (now - slot.lastRelevantTs >= 60)
                    slot.weight = std::max(0.0, slot.weight - kPerMinuteDecay);
            }
            for (std::size_t index = 0; index < groupSlots.size();)
            {
                if (groupSlots[index].weight < kWeightClose)
                {
                    groupSlots.erase(groupSlots.begin() + static_cast<long>(index));
                    continue;
                }
                ++index;
            }
        }
    }

    for (auto &[groupId, groupSlots] : slotsPerGroup)
    {
        for (TopicSlot &slot : groupSlots)
        {
            if (slot.state != SlotState::Engaged || !slot.pendingBurst)
                continue;
            // lull：相关突发后静默 kLullSeconds；或挂起超过 kMaxWaitSeconds 兜底
            const std::int64_t sinceRelevant = now - slot.lastRelevantTs;
            const bool lull = sinceRelevant >= kLullSeconds;
            const bool overdue = now - slot.lastEvaluatedTs >= kMaxWaitSeconds;
            if (!(lull || overdue))
                continue;
            // 礼貌窗与群级节流
            if (now - slot.lastSpokeTs < kSpeechCooldown)
                continue;
            bool groupThrottled = false;
            for (const TopicSlot &other : groupSlots)
            {
                if (now - other.lastSpokeTs < kGroupSpeechInterval)
                    groupThrottled = true;
            }
            if (groupThrottled)
                continue;
            slot.pendingBurst = false;
            slot.lastEvaluatedTs = now;
            due.push_back(groupId);
            break; // 每群每拍至多一次评估
        }
    }
    return due;
}

std::string TopicTracker::prepareEvaluation(std::uint64_t groupId, std::int64_t now)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = slotsPerGroup.find(groupId);
    if (it == slotsPerGroup.end())
        return {};

    TopicSlot *chosen = nullptr;
    std::int64_t newest = 0;
    for (TopicSlot &slot : it->second)
    {
        if (slot.state == SlotState::Engaged && slot.lastRelevantTs >= newest)
        {
            chosen = &slot;
            newest = slot.lastRelevantTs;
        }
    }
    if (chosen == nullptr || chosen->memberSeqs.empty())
        return {};
    (void)now;

    // 材料头：槽位成员 seq 列表（service 负责取记录拼 prompt）
    std::string header;
    for (std::int64_t seq : chosen->memberSeqs)
        header += std::to_string(seq) + ",";
    return header;
}

bool TopicTracker::allowJoinEvaluation(std::uint64_t groupId, std::int64_t now)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto &history = joinEvalHistory[groupId];
    while (!history.empty() && now - history.front() >= kJoinFuseWindow)
        history.pop_front();
    if (history.size() >= 1)
        return false;
    history.push_back(now);
    return true;
}

std::vector<TopicSlot> TopicTracker::slotsOf(std::uint64_t groupId) const
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = slotsPerGroup.find(groupId);
    return it == slotsPerGroup.end() ? std::vector<TopicSlot>{} : it->second;
}
