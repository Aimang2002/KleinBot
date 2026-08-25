#ifndef ONEBOT_MESSAGE_ENCODER_H
#define ONEBOT_MESSAGE_ENCODER_H

#include "OneBotAction.h"
#include "../../Port/OutboundDelivery.h"

#include <string>

class OneBotMessageEncoder
{
public:
    OneBotAction encode(const OutboundDelivery &delivery) const;

private:
    static nlohmann::json toSegments(const OutboundMessage &message);
    static long long parseNumericId(const std::string &id);
};

#endif // ONEBOT_MESSAGE_ENCODER_H
