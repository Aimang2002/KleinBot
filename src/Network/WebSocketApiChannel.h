#ifndef WEBSOCKET_API_CHANNEL_H
#define WEBSOCKET_API_CHANNEL_H

#include "OneBotApiChannel.h"
#include "../Protocol/OneBot/OneBotAction.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

// WebSocket 模式的 API 通道：call() 把带 echo 的 action 压入内部队列，
// 由 OneBotWebSocketSession 的写循环发出（优先于业务投递）；响应帧由读循环
// resolve 兑现。会话断开时 failAll 把所有未决调用以 networkError 兑现。
// echo 跨重连持续自增，不重置，避免撞上上一条连接的迟到响应
class WebSocketApiChannel final : public OneBotApiChannel
{
public:
    explicit WebSocketApiChannel(std::size_t maxPending = 64);

    OneBotApiResult call(const std::string &action, nlohmann::json params,
                         std::chrono::milliseconds timeout) override;

    // ---- 以下仅由传输线程（OneBotWebSocketSession）调用 ----
    bool tryPopAction(OneBotAction &out);
    void resolve(OneBotApiResult result);
    void failAll(const std::string &reason);

private:
    std::atomic<std::uint64_t> nextEcho{1};
    std::queue<OneBotAction> actionQueue;
    std::mutex mutex;
    std::unordered_map<std::int64_t, std::promise<OneBotApiResult>> pending;
    std::size_t maxPending;
};

#endif // WEBSOCKET_API_CHANNEL_H
