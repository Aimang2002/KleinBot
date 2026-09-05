#ifndef MESSAGE_OPTIONS_H
#define MESSAGE_OPTIONS_H

#include "../Application/BotIdentity.h"

struct MessageOptions
{
    BotIdentity bot;
    bool groupChatEnabled = true;
    // 群聊回复默认引用原消息并 @ 发起人（persona.humanize.quote_reply）
    bool humanizeQuoteReply = true;
};

#endif
