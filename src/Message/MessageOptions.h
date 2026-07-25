#ifndef MESSAGE_OPTIONS_H
#define MESSAGE_OPTIONS_H

#include "../Application/BotIdentity.h"

struct MessageOptions
{
    BotIdentity bot;
    bool groupChatEnabled = true;
};

#endif
