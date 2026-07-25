#ifndef ANTHROPIC_STANDARD_H
#define ANTHROPIC_STANDARD_H

#include "../../Port/ChatResponse.h"
#include "../../Port/ImageResponse.h"
#include "../../Port/VisionResponse.h"
#include "../../Port/LLMPort.h"
#include "../../Port/ChatRequest.h"
#include <curl/curl.h>

class AnthropicStandard : public LLMPort
{
public:
    explicit AnthropicStandard(std::string proxy = {}) : proxy(std::move(proxy)) {}
    ChatResponse request_chat(const ChatModel &model, const std::string &model_name, const ChatRequest &request) override;
    VisionResponse request_vision(const ChatModel &model, const std::string &model_name, const std::string &prompt, const std::string &base64) override;
    ImageResponse request_image(const ChatModel &model, const std::string &model_name, const std::string &prompt) override;

private:
    std::pair<std::string, long> http_post(const std::string &url, const std::string &api_key, const std::string &payload);

    ChatResponse chat_json_parse(const std::string &response);

    VisionResponse vision_json_parse(const std::string &response);

    static size_t write_callback_chat(char *ptr, size_t size, size_t nmemb, void *userdata);

    std::string filterNonNormalChars(std::string str);

    void VerifyCertificate(CURL *curl);
    std::string proxy;
};

#endif // ANTHROPIC_STANDARD_H
