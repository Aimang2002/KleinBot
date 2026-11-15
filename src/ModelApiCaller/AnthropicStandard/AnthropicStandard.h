#ifndef ANTHROPIC_STANDARD_H
#define ANTHROPIC_STANDARD_H

#include "../../Port/ChatResponse.h"
#include "../../Port/ImageResponse.h"
#include "../../Port/VisionResponse.h"
#include "../../Port/LLMPort.h"
#include "../../Port/ChatRequest.h"
#include "../../utils/Live.h"
#include "../DockOptions.h"
#include <atomic>
#include <curl/curl.h>

class AnthropicStandard : public LLMPort
{
public:
    // network：组合根的活值（ADR-023），每次请求开头取一次代理，network.proxy 改了下一次请求即用新代理
    explicit AnthropicStandard(const Live<DockOptions> &network, const std::atomic<bool> *running = nullptr)
        : network(network), running(running) {}
    ChatResponse request_chat(const ChatModel &model, const std::string &model_name, const ChatRequest &request) override;
    VisionResponse request_vision(const ChatModel &model, const std::string &model_name, const std::string &prompt, const std::string &base64) override;
    ImageResponse request_image(const ChatModel &model, const std::string &model_name, const std::string &prompt) override;

    // 公开供单元测试直接验证响应解析；生产路径仍由 request_chat 内部调用
    ChatResponse chat_json_parse(const std::string &response);

private:
    // proxy：本次请求开头取到的代理（空 = 直连）；timeoutMs：0 = CurlRequestControl 默认超时
    std::pair<std::string, long> http_post(const std::string &proxy, const std::string &url,
                                           const std::string &api_key, const std::string &payload,
                                           long timeoutMs = 0);

    VisionResponse vision_json_parse(const std::string &response);

    static size_t write_callback_chat(char *ptr, size_t size, size_t nmemb, void *userdata);

    std::string filterNonNormalChars(std::string str);

    const Live<DockOptions> &network;
    const std::atomic<bool> *running;
};

#endif // ANTHROPIC_STANDARD_H
