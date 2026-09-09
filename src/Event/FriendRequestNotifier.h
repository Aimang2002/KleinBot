#ifndef FRIEND_REQUEST_NOTIFIER_H
#define FRIEND_REQUEST_NOTIFIER_H

#include "../Application/EventRouter.h"
#include "../Port/MessageSenderPort.h"

#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

class OneBotApiChannel;

// 好友申请通知（T6）：私聊转告管理员（QQ 号、验证语、flag），绝不代为处理
// ——set_friend_add_request 属 v2.4.4，本版本只做观察与通报。
// flag 去重（内存，1h TTL）：实现端重发同一申请不重复打扰。
// 附带 echo 地基的首个 get_* 实战：get_stranger_info 取申请人昵称，失败则省略
class FriendRequestNotifier : public EventHandler
{
public:
    using Clock = std::function<std::int64_t()>;

    FriendRequestNotifier(MessageSenderPort &sender, OneBotApiChannel &api,
                          std::uint64_t managerId, Clock clock = {});

    void handle(const InboundMessage &event) override;

private:
    MessageSenderPort &sender_;
    OneBotApiChannel &api_;
    std::uint64_t managerId_;
    Clock clock_;

    std::mutex mutex_;
    std::unordered_map<std::string, std::int64_t> seen_; // flag → 首次见到时刻
};

#endif // FRIEND_REQUEST_NOTIFIER_H
