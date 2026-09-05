#ifndef ONEBOT_ACTION_H
#define ONEBOT_ACTION_H

#include "../../../Library/nlohmann/json.hpp"

#include <cstdint>
#include <string>

struct OneBotAction
{
    std::string action;
    nlohmann::json params;
    std::int64_t echo = 0; // >0 时 toJson 输出 echo 字段，供响应帧关联请求

    nlohmann::json toJson() const
    {
        // params 缺省时 nlohmann 是 null，部分实现端不接受，统一兜底为空对象
        nlohmann::json document = {
            {"action", action},
            {"params", params.is_null() ? nlohmann::json::object() : params}
        };
        if (echo > 0)
            document["echo"] = echo;
        return document;
    }
};

#endif // ONEBOT_ACTION_H
