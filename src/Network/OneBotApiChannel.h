#ifndef ONEBOT_API_CHANNEL_H
#define ONEBOT_API_CHANNEL_H

#include "../Protocol/OneBot/OneBotApiResult.h"

#include "../../../Library/nlohmann/json.hpp"

#include <chrono>
#include <string>

// API 调用通道：带 echo 关联的请求-响应语义。
// 与 fire-and-forget 的 OutboundMessageQueue（业务投递，可丢）相对，
// 调用方在等答案；后续 get_* 类能力与能力探测（v2.4.1 T2）都依赖本接口
class OneBotApiChannel
{
public:
    virtual ~OneBotApiChannel() = default;

    virtual OneBotApiResult call(const std::string &action, nlohmann::json params,
                                 std::chrono::milliseconds timeout = std::chrono::seconds(10)) = 0;
};

#endif // ONEBOT_API_CHANNEL_H
