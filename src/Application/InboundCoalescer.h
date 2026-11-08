#ifndef INBOUND_COALESCER_H
#define INBOUND_COALESCER_H

#include "../Port/InboundMessage.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// 入站连发合并（R2.2，2026-11-08，私聊范围）：同一用户短时间内连发的纯文本私聊
// 先攒成一个连发桶，静默/满额/超时后整桶交给该用户的执行 lane。
// 用户定规（铁律）：合并**只改时机，不改回复条数**——桶里的消息原样逐条保留、
// 逐条处理、逐条回复（N 进 N 出），顺序由代码保证；绝不拼接成一条交给模型。
// 纯逻辑、无锁：只由主线程调用（主循环 tryPop 之后）；不新增线程

// 一次放行的连发：原消息逐条保留，不拼接
struct Burst
{
    std::uint64_t user_id = 0;
    std::vector<InboundMessage> messages;
};

class InboundCoalescer
{
public:
    // 内部常量，用户决定暂不做配置项（2026-11-08）
    static constexpr std::int64_t kSilenceMs = 1500;   // 自最后一条起静默这么久即放行
    static constexpr std::size_t kMaxBurst = 5;        // 攒满这么多条立即放行
    static constexpr std::int64_t kMaxWaitMs = 10000;  // 自第一条起最多等这么久

    // 毫秒时钟（单调）；空 = std::chrono::steady_clock。测试注入假时钟
    using Clock = std::function<std::int64_t()>;
    explicit InboundCoalescer(Clock clock = {});

    std::int64_t nowMs() const;

    // 是否进入连发桶：私聊 message 事件、纯文本（无图片/附件/表情段）、非 # 命令。
    // 群聊一律直通：群 @ 路径每条有自己的决策记录与回执，合并留到 enforce 之后
    static bool coalescable(const InboundMessage &message);

    // 可合并的消息进入该用户的桶并返回空；不可合并的立即放行——放行前先把同一
    // 用户桶里已攒的消息按序 flush（保证该用户的处理顺序），再放行它自己（单条 Burst）
    std::vector<Burst> offer(InboundMessage message, std::int64_t nowMs);

    // 主循环每 tick 调一次：静默 ≥ kSilenceMs、条数 ≥ kMaxBurst 或自首条 ≥ kMaxWaitMs 的桶整桶放行
    std::vector<Burst> flushDue(std::int64_t nowMs);

    // 退出链用：取出全部剩余桶（主循环在 running=false 后只记日志，不再提交）
    std::vector<Burst> flushAll();

    // 桶里尚未放行的消息总数（观测/日志）
    std::size_t pendingCount() const;

private:
    struct Bucket
    {
        std::vector<InboundMessage> messages;
        std::int64_t firstMs = 0;
        std::int64_t lastMs = 0;
    };

    Clock clock;
    std::map<std::uint64_t, Bucket> buckets;
};

// 连发注记（事实性，不含"必须怎么答"的规则）：第 index 条（0 起）之后同一连发里
// 还有哪些消息、原文是什么、会各自单独回复。index 是最后一条（含单条 Burst）时返回空串，
// 单条消息的请求因此与合并前逐字相同。经 ChatService 的尾插注记（appendContextNote）进入请求副本
std::string burstFollowUpNote(const std::vector<InboundMessage> &messages, std::size_t index);

#endif
