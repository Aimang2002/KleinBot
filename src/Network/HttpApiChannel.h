#ifndef HTTP_API_CHANNEL_H
#define HTTP_API_CHANNEL_H

#include "OneBotApiChannel.h"

#include <atomic>
#include <chrono>
#include <string>

// HTTP 模式的 API 通道：curl POST ${base_url}/${action} 同步等响应体，
// 天然请求-响应同体，无需 echo 关联表（echo 仍随 body 发出便于实现端日志对照）
class HttpApiChannel final : public OneBotApiChannel
{
public:
    HttpApiChannel(std::string apiBaseUrl, std::string apiAuthToken,
                   long connectTimeoutMs, long requestTimeoutMs,
                   const std::atomic<bool> *running = nullptr);

    OneBotApiResult call(const std::string &action, nlohmann::json params,
                         std::chrono::milliseconds timeout) override;

private:
    std::string apiBaseUrl;
    std::string apiAuthToken;
    long connectTimeoutMs;
    long requestTimeoutMs;
    const std::atomic<bool> *running;
};

#endif // HTTP_API_CHANNEL_H
