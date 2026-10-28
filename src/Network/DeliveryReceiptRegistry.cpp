#include "DeliveryReceiptRegistry.h"

#include "../Log/Log.h"
#include "../Protocol/OneBot/OneBotMessageId.h"

#include <ctime>
#include <utility>
#include <vector>

DeliveryReceiptRegistry::DeliveryReceiptRegistry(DeliveryReceiptSink *sink,
                                                 std::function<std::int64_t()> clock,
                                                 std::int64_t timeoutSeconds)
    : sink(sink),
      clock(clock ? std::move(clock)
                  : [] { return static_cast<std::int64_t>(std::time(nullptr)); }),
      timeoutSeconds(timeoutSeconds)
{
}

std::int64_t DeliveryReceiptRegistry::track(const std::string &correlationId)
{
    const std::int64_t echo = nextEcho.fetch_add(1);
    std::lock_guard<std::mutex> lock(mutex);
    pending.emplace(echo, Pending{correlationId, clock()});
    return echo;
}

bool DeliveryReceiptRegistry::resolve(const OneBotApiResult &result)
{
    if (result.echo < kEchoBase)
        return false;

    Pending entry;
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto node = pending.find(result.echo);
        if (node == pending.end())
        {
            // 本表 echo 段但查无未决：超时已按 unknown 兑现过，或重复帧
            LOG_DEBUG("投递回执无未决登记（超时已兑现或重复帧），echo=" +
                      std::to_string(result.echo));
            return true;
        }
        entry = std::move(node->second);
        pending.erase(node);
    }

    DeliveryReceipt receipt;
    receipt.correlation_id = entry.correlationId;
    receipt.retcode = result.retcode;
    if (result.networkError)
    {
        receipt.status = "unknown";
        receipt.detail = "无协议响应";
    }
    else if ((result.status == "ok" || result.status == "async") && result.retcode == 0)
    {
        // async：协议端已受理、异步发送，拿不到 message_id 也算 sent（有明确受理）
        receipt.status = "sent";
        receipt.message_id_raw = oneBotMessageIdRaw(result.data);
    }
    else
    {
        receipt.status = "failed";
        receipt.detail = "status=" + result.status + " retcode=" + std::to_string(result.retcode);
    }
    emit(std::move(receipt));
    return true;
}

void DeliveryReceiptRegistry::failAll(const std::string &reason)
{
    std::vector<Pending> drained;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (pending.empty())
            return;
        drained.reserve(pending.size());
        for (auto &entry : pending)
            drained.push_back(std::move(entry.second));
        pending.clear();
    }
    LOG_WARNING("投递回执：" + reason + "，" + std::to_string(drained.size()) +
                " 条投递送达状态未知");
    for (Pending &entry : drained)
    {
        DeliveryReceipt receipt;
        receipt.correlation_id = std::move(entry.correlationId);
        receipt.status = "unknown";
        receipt.detail = reason;
        emit(std::move(receipt));
    }
}

void DeliveryReceiptRegistry::sweep()
{
    const std::int64_t now = clock();
    std::vector<Pending> expired;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (now - it->second.trackedAt >= timeoutSeconds)
            {
                expired.push_back(std::move(it->second));
                it = pending.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    for (Pending &entry : expired)
    {
        DeliveryReceipt receipt;
        receipt.correlation_id = std::move(entry.correlationId);
        receipt.status = "unknown";
        receipt.detail = "回执超时";
        emit(std::move(receipt));
    }
}

std::size_t DeliveryReceiptRegistry::pendingCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return pending.size();
}

void DeliveryReceiptRegistry::emit(DeliveryReceipt receipt) const
{
    if (sink == nullptr)
        return;
    try
    {
        sink->onReceipt(std::move(receipt));
    }
    catch (const std::exception &error)
    {
        // 回执消费失败不能打断传输线程
        LOG_ERROR("投递回执处理异常：" + std::string(error.what()));
    }
}
