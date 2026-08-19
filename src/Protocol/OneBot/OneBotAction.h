#ifndef ONEBOT_ACTION_H
#define ONEBOT_ACTION_H

#include "../../../Library/nlohmann/json.hpp"

#include <string>

struct OneBotAction
{
    std::string action;
    nlohmann::json params;

    nlohmann::json toJson() const
    {
        return {
            {"action", action},
            {"params", params}
        };
    }
};

#endif // ONEBOT_ACTION_H
