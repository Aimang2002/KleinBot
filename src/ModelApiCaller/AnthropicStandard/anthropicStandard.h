#ifndef ANTHROPIC_STANDARD_H
#define ANTHROPIC_STANDARD_H

#include <iostream>
#include "../../ConfigManager/ConfigManager.h"
#include "../JsonParse/JsonParse.h"
#include <curl/curl.h>

struct AnthropicChatResponse
{
    std::string type;
    std::string role;
    std::string content_type;
    std::string text;        // 模型发来的内容
    std::string stop_reason; // API停止原因
};

class AnthropicStandard
{
public:
    AnthropicChatResponse send_to_chat(const std::string endpoint, const std::string key, const nlohmann::json &payload);

private:
};

#endif // ANTHROPIC_STANDARD_H