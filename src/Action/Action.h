#ifndef ACTION_H
#define ACTION_H

#include "../Port/OutboundMessage.h"
#include "../../Library/nlohmann/json.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct ActionDescriptor
{
    std::string name;
    std::string description;
    nlohmann::json parameters_schema;
    bool expose_as_tool = false;
    bool requires_admin = false;
};

struct ActionContext
{
    uint64_t user_id = 0;
    int64_t user_message_id = 0;
};

struct ActionResult
{
    std::string content;
    std::vector<OutboundMessage> outbound_messages;
    std::string context_content;
    bool terminal = true;
};

class Action
{
public:
    virtual ~Action() = default;
    virtual const ActionDescriptor &descriptor() const = 0;
    virtual ActionResult execute(const nlohmann::json &arguments,
                                 const ActionContext &context) = 0;
};

#endif // ACTION_H
