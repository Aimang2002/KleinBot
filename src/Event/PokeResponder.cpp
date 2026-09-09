#include "PokeResponder.h"

#include "../Application/CapabilityBroker.h"
#include "../Log/Log.h"
#include "../Network/OneBotApiChannel.h"
#include "../Port/OutboundMessage.h"

#include "../../Library/nlohmann/json.hpp"

#include <random>
#include <thread>

namespace
{
// 零旋钮（用户定规）：回应节奏与概率全部为常量，不进配置
constexpr std::int64_t kCooldownSeconds = 30;              // 同用户两次回应最小间隔
constexpr int kPokeBackProbabilityPercent = 15;            // 群聊反戳概率
constexpr std::int64_t kPokeBackIntervalSeconds = 3600;    // 反戳每用户间隔上限 1 小时 1 次
constexpr int kReplyDelayMinSeconds = 1;                   // 回应前短延迟：像人一样不会瞬间反应
constexpr int kReplyDelayMaxSeconds = 4;
constexpr int kPokeBackDelayMinSeconds = 2;                // 反戳延后：与回应错开
constexpr int kPokeBackDelayMaxSeconds = 5;

std::string displayName(const InboundMessage &event)
{
    if (!event.card.empty())
        return event.card;
    if (!event.nickname.empty())
        return event.nickname;
    return std::to_string(event.user_id);
}
}

PokeResponder::PokeResponder(PersonaReplier replier, MessageSenderPort &sender,
                             const CapabilityBroker &capabilities, OneBotApiChannel &api,
                             VoiceRenderer voiceRenderer, BotIdentity bot,
                             Clock clock, RandomIn randomIn, Sleeper sleeper)
    : replier_(std::move(replier)), sender_(sender), capabilities_(capabilities),
      api_(api), voiceRenderer_(std::move(voiceRenderer)), bot_(bot),
      clock_(clock ? std::move(clock) : std::function<std::int64_t()>(
                                            [] { return std::time(nullptr); })),
      randomIn_(randomIn ? std::move(randomIn)
                         : std::function<int(int, int)>([](int lo, int hi)
                                                       {
                                                           std::mt19937 engine{std::random_device{}()};
                                                           std::uniform_int_distribution<int> dist(lo, hi);
                                                           return dist(engine);
                                                       })),
      sleeper_(sleeper ? std::move(sleeper)
                       : std::function<void(std::chrono::milliseconds)>(
                             [](std::chrono::milliseconds duration)
                             { std::this_thread::sleep_for(duration); }))
{
}

void PokeResponder::handle(const InboundMessage &event)
{
    // 触发条件：戳的是 bot，且不是 bot 自己戳出去的回声；别人戳别的人不关我们的事
    if (event.target_id != bot_.id || event.user_id == bot_.id || event.user_id == 0)
        return;

    // 冷却先占坑再干活：事件按 user_id 占 lane 串行，同用户连戳只回第一下
    const std::int64_t now = clock_();
    {
        std::lock_guard<std::mutex> lock(cooldownMutex_);
        auto it = lastReply_.find(event.user_id);
        if (it != lastReply_.end() && now - it->second < kCooldownSeconds)
            return;
        lastReply_[event.user_id] = now;
    }

    // 短延迟占用该用户的 lane，不影响其他用户
    sleeper_(std::chrono::seconds(randomIn_(kReplyDelayMinSeconds, kReplyDelayMaxSeconds)));

    const bool inGroup = event.group_id != 0;
    // 场景交代要具体：这是系统通知、消息发到哪个会话、对什么做反应。
    // 含糊的"有人戳了你，请回应一句"会让模型输出"我在"这类应答式退化文本
    const std::string scene = inGroup ? "在群里戳了戳你。你的下一句QQ消息会发在这个群里"
                                      : "在私聊里戳了戳你。你的下一句QQ消息会发给对方";
    const std::string prompt = std::string("QQ戳一戳通知：") + displayName(event) +
                               scene +
                               "——像真人被戳了一下的自然反应：撒娇、吐槽、装不耐烦都可以，"
                               "符合你的性格，一到两句话。";
    const std::string reply = replier_(event.user_id, prompt);
    if (reply.empty())
    {
        LOG_WARNING("戳一戳回应生成失败，静默跳过：user_id=" + std::to_string(event.user_id));
        return;
    }

    OutboundDelivery delivery;
    if (inGroup)
        delivery.target = GroupMessageTarget{std::to_string(event.group_id)};
    else
        delivery.target = DirectMessageTarget{std::to_string(event.user_id)};

    if (voiceRenderer_)
    {
        if (auto audioPath = voiceRenderer_(event.user_id, reply))
            delivery.message = VoiceMessage{std::move(*audioPath)};
        else
            delivery.message = TextMessage{reply}; // TTS 失败回退文本
    }
    else
    {
        delivery.message = TextMessage{reply};
    }
    sender_.deliver(std::move(delivery));

    maybePokeBack(event, now);
}

void PokeResponder::maybePokeBack(const InboundMessage &event, std::int64_t now)
{
    // 反戳只发生在群聊；能力位门控（D12），不支持静默关闭零日志噪音
    if (event.group_id == 0 || !capabilities_.supports(BotFeature::GroupPoke))
        return;
    if (randomIn_(0, 99) >= kPokeBackProbabilityPercent)
        return;

    {
        std::lock_guard<std::mutex> lock(pokeBackMutex_);
        auto it = lastPokeBack_.find(event.user_id);
        if (it != lastPokeBack_.end() && now - it->second < kPokeBackIntervalSeconds)
            return;
        lastPokeBack_[event.user_id] = now;
    }

    sleeper_(std::chrono::seconds(randomIn_(kPokeBackDelayMinSeconds, kPokeBackDelayMaxSeconds)));
    nlohmann::json params;
    params["group_id"] = event.group_id;
    params["user_id"] = event.user_id;
    // fire-and-forget：反戳失败无所谓，不追查
    (void)api_.call("group_poke", std::move(params), std::chrono::seconds(5));
}
