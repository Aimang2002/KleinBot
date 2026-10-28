#ifndef DELIVERY_RECEIPT_REGISTRY_H
#define DELIVERY_RECEIPT_REGISTRY_H

#include "../Port/DeliveryReceipt.h"
#include "../Protocol/OneBot/OneBotApiResult.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

// 业务投递的 echo 关联表（R0.2 / B2）：WebSocket 模式下给带 correlation_id 的
// 投递分配 echo，响应帧回来时按 echo 找回 correlation 并向 sink 发回执。
//
// echo 空间与 WebSocketApiChannel（从 1 自增）分段：本表从 kEchoBase 起自增，
// 两者永不重叠，所以读循环可以先问本表、再交给 API 通道。
// 超时（默认 60s）与断连（failAll）都按 unknown 兑现——拿不到回执不等于失败。
// 组合根持有一个实例并注入传输线程；sink 可空（空时只做计数，不发回执）。
class DeliveryReceiptRegistry
{
public:
    static constexpr std::int64_t kEchoBase = static_cast<std::int64_t>(1) << 40;

    explicit DeliveryReceiptRegistry(DeliveryReceiptSink *sink = nullptr,
                                     std::function<std::int64_t()> clock = {},
                                     std::int64_t timeoutSeconds = 60);

    // 登记一次待回执投递，返回分配的 echo（>0）
    std::int64_t track(const std::string &correlationId);

    // 响应帧兑现：echo 在本表 → 发回执并返回 true；否则返回 false（交给 API 通道）
    bool resolve(const OneBotApiResult &result);

    // 断连/会话结束：全部未决按 unknown 兑现
    void failAll(const std::string &reason);

    // 超时清扫：超过 timeoutSeconds 未兑现的按 unknown 兑现；传输循环周期调用
    void sweep();

    std::size_t pendingCount() const;

private:
    struct Pending
    {
        std::string correlationId;
        std::int64_t trackedAt = 0;
    };

    void emit(DeliveryReceipt receipt) const;

    DeliveryReceiptSink *const sink;
    const std::function<std::int64_t()> clock;
    const std::int64_t timeoutSeconds;
    std::atomic<std::int64_t> nextEcho{kEchoBase};
    mutable std::mutex mutex;
    std::map<std::int64_t, Pending> pending;
};

#endif // DELIVERY_RECEIPT_REGISTRY_H
