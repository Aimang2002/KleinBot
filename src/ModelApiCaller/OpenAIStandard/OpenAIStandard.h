
#ifndef OPENAI_STANDARD_H
#define OPENAI_STANDARD_H

#include "../../Port/ChatResponse.h"
#include "../../Port/ImageResponse.h"
#include "../../Port/VisionResponse.h"
#include "../../Port/LLMPort.h"
#include "../../Port/ChatRequest.h"
#include <curl/curl.h>

// OpenAIStandard类
class OpenAIStandard : public LLMPort
{
public:
    ChatResponse request_chat(const ChatModel &model, const std::string &model_name, const ChatRequest &request) override;

    VisionResponse request_vision(const ChatModel &model, const std::string &model_name, const std::string &prompt, const std::string &base64) override;

    ImageResponse request_image(const ChatModel &model, const std::string &model_name, const std::string &prompt) override;

private:
    std::pair<std::string, long> http_post(const std::string &url, const std::string &api_key, const std::string &payload);

    ChatResponse chat_json_parse(const std::string &response);

    ImageResponse draw_json_parse(const std::string &response);

    VisionResponse vision_json_parse(const std::string &response);

    // 回调函数
    static size_t write_callback_chat(char *ptr, size_t size, size_t nmemb, void *userdata);

    // API和端点修正
    std::string filterNonNormalChars(std::string str);

    // Response json 合法性验证
    //  std::string ResponseJsonVerify(std::string str, std::string sub = "}}");

    // 证证书合法性(windows下)
    void VerifyCertificate(CURL *curl);

private:
};

#endif // OPENAI_STANDARD_H