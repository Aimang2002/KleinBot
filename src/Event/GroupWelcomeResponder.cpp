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
                                             BotIdentity bot, Clock clock)
    : replier_(std::move(replier)), sender_(sender), bot_(bot),
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

    // 事件+任务框架（真机教训：'通知'字样会让模型把自己摆到接收方位置，
    // 输出"收到"这类应答；任务指令放最后+硬性内容要求才能压住退化输出）
    const std::string who = displayName(event);
    const std::string prompt = std::string("[系统事件] ") + who +
                               " 刚刚加入了群聊。\n你的任务：立即在这个群里发出一条欢迎消息——"
                               "直接向 " + who +
                               " 打招呼（带上TA的名字），表达欢迎，符合你的性格，自然口语，"
                               "一到两句话。只输出这条消息本身，不要确认或转述这条事件。";
    const std::string reply = replier_(event.user_id, prompt);
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
