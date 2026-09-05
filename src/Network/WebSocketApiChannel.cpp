#include "WebSocketApiChannel.h"
#include "../Log/Log.h"

WebSocketApiChannel::WebSocketApiChannel(std::size_t maxPending)
    : maxPending(maxPending)
{
}

OneBotApiResult WebSocketApiChannel::call(const std::string &action, nlohmann::json params,
                                          std::chrono::milliseconds timeout)
{
    std::promise<OneBotApiResult> promise;
    std::future<OneBotApiResult> future = promise.get_future();
    std::int64_t echo = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (pending.size() >= maxPending)
        {
            LOG_WARNING("OneBot API 未决调用已达上限 " + std::to_string(maxPending) +
                        "，拒绝：" + action);
            OneBotApiResult rejected;
            rejected.networkError = true;
            return rejected;
        }
        echo = static_cast<std::int64_t>(nextEcho.fetch_add(1));
        OneBotAction outgoing;
        outgoing.action = action;
        outgoing.params = std::move(params);
        outgoing.echo = echo;
        pending.emplace(echo, std::move(promise));
        actionQueue.push(std::move(outgoing));
    }

    if (future.wait_for(timeout) != std::future_status::ready)
    {
        // 超时摘除；迟到的响应帧会在 resolve 处因查无此 echo 被丢弃。
        // 已入队尚未写出的 action 仍会在下一条连接上发出（实现端未收到过，不算重复），
        // 此时调用方已按失败处理——窗口极小，T1 接受该语义
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending.erase(echo);
        }
        OneBotApiResult timeoutResult;
        timeoutResult.echo = echo;
        timeoutResult.networkError = true;
        return timeoutResult;
    }
    return future.get();
}

bool WebSocketApiChannel::tryPopAction(OneBotAction &out)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (actionQueue.empty())
        return false;
    out = std::move(actionQueue.front());
    actionQueue.pop();
    return true;
}

void WebSocketApiChannel::resolve(OneBotApiResult result)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto node = pending.find(result.echo);
    if (node == pending.end())
    {
        LOG_DEBUG("OneBot API 响应无未决请求（超时已放弃或重复帧），echo=" +
                  std::to_string(result.echo));
        return;
    }
    node->second.set_value(std::move(result));
    pending.erase(node);
}

void WebSocketApiChannel::failAll(const std::string &reason)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (pending.empty())
        return;
    for (auto &entry : pending)
    {
        OneBotApiResult failed;
        failed.echo = entry.first;
        failed.networkError = true;
        entry.second.set_value(std::move(failed));
    }
    LOG_WARNING("OneBot API 通道：" + reason + "，已按失败兑现 " +
                std::to_string(pending.size()) + " 个未决调用");
    pending.clear();
}
