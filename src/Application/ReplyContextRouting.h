#ifndef REPLY_CONTEXT_ROUTING_H
#define REPLY_CONTEXT_ROUTING_H

#include "../Application/BotIdentity.h"
#include "../Port/InboundMessage.h"
#include "../Port/OutboundDelivery.h"

#include <algorithm>
#include <optional>

// 群聊回复的指向性决策（纯函数，行为固定无配置）：
//   消息 @ 了 bot（点名应答）→ 本轮第一条出站引用原消息 + @ 发起人
//   同一轮的后续分条/多消息 → attach=false，裸消息，不再重复指向同一条 @
//   没有 @ bot（未来接话/观察路径）→ 裸消息，像群友随口插话
// bot 自身消息的回声不产生指向。私聊由调用方直接不带 reply
inline std::optional<ReplyContext> buildReplyContext(const InboundMessage &data,
                                                     const BotIdentity &bot,
                                                     bool attach = true)
{
    if (!attach)
    {
        return std::nullopt;
    }

    const auto mentioned = std::find(data.mentioned_ids.begin(), data.mentioned_ids.end(),
                                     bot.id);
    if (mentioned == data.mentioned_ids.end() || data.user_id == bot.id)
    {
        return std::nullopt;
    }

    ReplyContext reply;
    reply.message_id = data.message_id;
    reply.message_id_raw = data.message_id_raw;
    reply.at_user_id = std::to_string(data.user_id);
    return reply;
}

#endif // REPLY_CONTEXT_ROUTING_H
