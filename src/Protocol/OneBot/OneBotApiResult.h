#ifndef ONEBOT_API_RESULT_H
#define ONEBOT_API_RESULT_H

#include "../../../Library/nlohmann/json.hpp"

#include <cstdint>
#include <string>

// OneBot API 调用的统一回报：WS 响应帧与 HTTP 响应体同形。
// networkError=true 表示没拿到协议响应（超时/断连/通道拒绝），此时其余字段无意义
struct OneBotApiResult
{
    std::int64_t echo = 0;
    std::string status;         // "ok" / "async" / "failed"
    std::int64_t retcode = 0;
    nlohmann::json data;
    bool networkError = false;
};

#endif // ONEBOT_API_RESULT_H
