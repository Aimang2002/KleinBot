#include "GroupWelcomeResponder.h"

#include "../Log/Log.h"

namespace
{
constexpr std::int64_t kWelcomeCooldownSeconds = 600; // 群冷却：批量进人只欢迎第一个

std::string displayName(const InboundMessage &event)
{
    if (!event.card.empty())
        return event.card;
    if (!event.nickname.empty())
        return event.nickname;
    return std::to_string(event.user_id);
}
}

GroupWelcomeResponder::GroupWelcomeResponder(PersonaReplier replier, MessageSenderPort &sender,
                                             BotIdentity bot, std::uint64_t personaUserId,
                                             Clock clock)
    : replier_(std::move(replier)), sender_(sender), bot_(bot),
      personaUserId_(personaUserId),
      clock_(clock ? std::move(clock) : std::function<std::int64_t()>(
                                            [] { return std::time(nullptr); }))
{
}

void GroupWelcomeResponder::handle(const InboundMessage &event)
{
    if (event.notice_type == "group_decrease")
    {
        // 离群默认不说话（零旋钮：无 farewell 开关，仅留观测日志）
        LOG_INFO("群 " + std::to_string(event.group_id) + " 成员离群（" +
                 (event.sub_type.empty() ? "unknown" : event.sub_type) + "）：user_id=" +
                 std::to_string(event.user_id) + "，按默认策略不发言");
        return;
    }

    // group_increase：bot 自己被拉进新群时保持克制，不自动发言
    if (event.user_id == bot_.id)
    {
        LOG_INFO(bot_.name + " 已加入群 " + std::to_string(event.group_id) +
                 "，不自动发言");
        return;
    }

    const std::int64_t now = clock_();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = lastWelcome_.find(event.group_id);
        if (it != lastWelcome_.end() && now - it->second < kWelcomeCooldownSeconds)
            return;
        lastWelcome_[event.group_id] = now;
    }

    // 纯自然叙事（真机教训：三轮迭代证明任何元框架都会被模型当成需要应答的
    // 对话——"我在"/"收到"/"未收到事件内容"均由此而来，方括号标记最严重）。
    // 要求里放应答无法满足的硬性内容（带上名字），结尾钉死输出物
    const std::string who = displayName(event);
    const std::string prompt = "咱们群刚来了一位新成员，是 " + who +
                               "。你以群友的身份跟TA打个招呼、欢迎一下——像平时群聊那样自然，"
                               "带上TA的名字，一两句话就够。你接下来这句话会直接发到群里，"
                               "所以只说欢迎那句话，别的不用说。";
    const std::string reply = replier_(personaUserId_ != 0 ? personaUserId_ : event.user_id,
                                       prompt);
    if (reply.empty())
    {
        LOG_WARNING("进群欢迎生成失败，静默跳过：group_id=" +
                    std::to_string(event.group_id));
        return;
    }

    OutboundDelivery delivery;
    delivery.target = GroupMessageTarget{std::to_string(event.group_id)};
    delivery.message = TextMessage{reply};
    sender_.deliver(std::move(delivery));
}
