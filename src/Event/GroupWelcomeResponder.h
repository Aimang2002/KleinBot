#ifndef GROUP_WELCOME_RESPONDER_H
#define GROUP_WELCOME_RESPONDER_H

#include "../Application/BotIdentity.h"
#include "../Application/EventRouter.h"
#include "../Port/MessageSenderPort.h"
#include "PersonaReplier.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

// 进群欢迎 / 离群静默（T6）：新成员进群 → 人格化欢迎一句；
// bot 自己进群与成员离群 → 仅日志不发言（退群原因复杂，乱说有社交风险）。
// 群冷却 600s：炸群、批量拉人时只欢迎第一个——无律法层之前的结构保险
class GroupWelcomeResponder : public EventHandler
{
public:
    using Clock = std::function<std::int64_t()>;

    GroupWelcomeResponder(PersonaReplier replier, MessageSenderPort &sender,
                          BotIdentity bot, Clock clock = {});

    void handle(const InboundMessage &event) override;

private:
    PersonaReplier replier_;
    MessageSenderPort &sender_;
    BotIdentity bot_;
    Clock clock_;

    std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::int64_t> lastWelcome_; // group_id → 上次欢迎时刻
};

#endif // GROUP_WELCOME_RESPONDER_H
