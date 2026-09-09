#ifndef POKE_RESPONDER_H
#define POKE_RESPONDER_H

#include "../Application/BotIdentity.h"
#include "../Application/EventRouter.h"
#include "../Port/MessageSenderPort.h"
#include "PersonaReplier.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

class CapabilityBroker;
class OneBotApiChannel;

// 戳一戳回应（T6）：被戳 → 人格化回一句，群里偶尔反戳回去。
// 结构防线（律法层缺位前的保险）：同用户 30s 冷却只回第一下；反戳要求
// 群聊 + GroupPoke 能力位 + 15% 概率 + 每用户每小时至多 1 次——
// 结构上不可能演变成戳一戳大战或刷屏
class PokeResponder : public EventHandler
{
public:
    // 可注入 seam：生产绑系统实现，测试注入确定性 fake
    using Clock = std::function<std::int64_t()>;
    using RandomIn = std::function<int(int lo, int hi)>;
    using Sleeper = std::function<void(std::chrono::milliseconds)>;
    // 语音渲染：语音模式且 TTS 成功返回音频路径；否则 nullopt → 回退文本
    using VoiceRenderer = std::function<std::optional<std::string>(
        std::uint64_t user_id, const std::string &text)>;

    PokeResponder(PersonaReplier replier, MessageSenderPort &sender,
                  const CapabilityBroker &capabilities, OneBotApiChannel &api,
                  VoiceRenderer voiceRenderer, BotIdentity bot,
                  Clock clock = {}, RandomIn randomIn = {}, Sleeper sleeper = {});

    void handle(const InboundMessage &event) override;

private:
    void maybePokeBack(const InboundMessage &event, std::int64_t now);

    PersonaReplier replier_;
    MessageSenderPort &sender_;
    const CapabilityBroker &capabilities_;
    OneBotApiChannel &api_;
    VoiceRenderer voiceRenderer_;
    BotIdentity bot_;
    Clock clock_;
    RandomIn randomIn_;
    Sleeper sleeper_;

    std::mutex cooldownMutex_;
    std::unordered_map<std::uint64_t, std::int64_t> lastReply_;     // user_id → 上次回应时刻
    std::mutex pokeBackMutex_;
    std::unordered_map<std::uint64_t, std::int64_t> lastPokeBack_;  // user_id → 上次反戳时刻
};

#endif // POKE_RESPONDER_H
