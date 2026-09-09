#include "FriendRequestNotifier.h"

#include "../Log/Log.h"
#include "../Network/OneBotApiChannel.h"
#include "../Port/OutboundMessage.h"

namespace
{
constexpr std::int64_t kFlagTtlSeconds = 3600; // 同一 flag 一小时内只通知一次
}

FriendRequestNotifier::FriendRequestNotifier(MessageSenderPort &sender, OneBotApiChannel &api,
                                             std::uint64_t managerId, Clock clock)
    : sender_(sender), api_(api), managerId_(managerId),
      clock_(clock ? std::move(clock) : std::function<std::int64_t()>(
                                            [] { return std::time(nullptr); }))
{
}

void FriendRequestNotifier::handle(const InboundMessage &event)
{
    if (managerId_ == 0)
    {
        LOG_INFO("收到好友申请：user_id=" + std::to_string(event.user_id) +
                 "，未配置管理员，无法通报");
        return;
    }

    // flag 去重：顺带淘汰超 TTL 的旧记录，防止集合无界增长
    const std::int64_t now = clock_();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = seen_.begin(); it != seen_.end();)
        {
            if (now - it->second >= kFlagTtlSeconds)
                it = seen_.erase(it);
            else
                ++it;
        }
        if (!seen_.emplace(event.flag, now).second)
            return;
    }

    // 申请人昵称尽力而为：查询失败省略，不影响通报主链路
    std::string nickname;
    nlohmann::json probe;
    probe["user_id"] = event.user_id;
    const OneBotApiResult info = api_.call("get_stranger_info", std::move(probe),
                                           std::chrono::seconds(3));
    if (!info.networkError && info.retcode == 0 && info.data.contains("nickname"))
        nickname = info.data.value("nickname", "");

    std::string text = "收到好友申请：QQ " + std::to_string(event.user_id) +
                       (nickname.empty() ? "" : "（" + nickname + "）") +
                       "，验证语：" + (event.comment.empty() ? "（无）" : event.comment) +
                       "，处理令牌：" + event.flag +
                       "。自动处理将在后续版本提供，请手动处理。";
    LOG_INFO("好友申请已通报管理员：user_id=" + std::to_string(event.user_id) +
             "，flag=" + event.flag);
    OutboundDelivery delivery;
    delivery.target = DirectMessageTarget{std::to_string(managerId_)};
    delivery.message = TextMessage{std::move(text)};
    sender_.deliver(std::move(delivery));
}
