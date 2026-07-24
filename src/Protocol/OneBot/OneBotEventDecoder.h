#ifndef ONEBOT_EVENT_DECODER_H
#define ONEBOT_EVENT_DECODER_H

#include "../../Port/InboundMessage.h"

#include <optional>
#include <string>

class OneBotEventDecoder
{
public:
    std::optional<InboundMessage> decode(const std::string &payload) const;
};

#endif // ONEBOT_EVENT_DECODER_H
