#ifndef LLMPORT_H
#define LLMPORT_H

#include <iostream>
#include "../ModelRegistry/ChatModel.h"
#include "../../Library/nlohmann/json.hpp"
#include "ChatResponse.h"
#include "VisionResponse.h"
#include "ImageResponse.h"
#include "ChatRequest.h"

class LLMPort
{
public:
    virtual ChatResponse request_chat(const ChatModel &model, const std::string &model_name, const ChatRequest &request) = 0;
    virtual VisionResponse request_vision(const ChatModel &model, const std::string &model_name, const std::string &prompt, const std::string &base64) = 0;
    virtual ImageResponse request_image(const ChatModel &model, const std::string &model_name, const std::string &prompt) = 0;
    virtual ~LLMPort() = default;
};

#endif // LLMPORT_H