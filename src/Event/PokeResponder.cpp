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
                             std::uint64_t personaUserId,
                             Clock clock, RandomIn randomIn, Sleeper sleeper)
    : replier_(std::move(replier)), sender_(sender), capabilities_(capabilities),
      api_(api), voiceRenderer_(std::move(voiceRenderer)), bot_(bot),
      personaUserId_(personaUserId),
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
    // 纯自然叙事（真机教训：元框架会被模型当成需要应答的对话，产出
    // "我在/收到/未收到事件内容"这类退化应答，方括号标记最严重）
    const std::string scene = inGroup ? "在群里" : "在私聊";
    const std::string prompt = displayName(event) + " 刚刚" + scene +
                               "戳了戳你——就是QQ的戳一戳，你被TA戳了一下。回TA一句话吧，"
                               "像真人被戳到的反应：可以撒娇、可以吐槽、也可以装不耐烦，"
                               "看你的性格来，一两句话就够。你接下来这句话会直接发给TA，"
                               "所以只说那句话，别的不用说。";
    const std::string reply = replier_(personaUserId_ != 0 ? personaUserId_ : event.user_id,
                                       prompt);
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
