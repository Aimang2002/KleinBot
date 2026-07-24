#ifndef OUTBOUND_DELIVERY_H
#define OUTBOUND_DELIVERY_H

#include "OutboundMessage.h"

#include <string>
#include <variant>

struct DirectMessageTarget
{
    std::string user_id;
};

struct GroupMessageTarget
{
    std::string group_id;
};

using OutboundTarget = std::variant<DirectMessageTarget, GroupMessageTarget>;

struct OutboundDelivery
{
    OutboundTarget target;
    OutboundMessage message;
};

#endif // OUTBOUND_DELIVERY_H
