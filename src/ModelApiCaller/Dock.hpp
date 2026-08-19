#ifndef DOCK_H
#define DOCK_H

#include "OpenAIStandard/OpenAIStandard.h"
#include "AnthropicStandard/AnthropicStandard.h"
#include "../Message/Person.hpp"
#include "../Port/ChatResponse.h"
#include "../Port/VisionResponse.h"
#include "../Port/ImageResponse.h"
#include "../Port/LLMPort.h"
#include "../Log/Log.h"
#include "DockOptions.h"
#include <atomic>
#include <string>
#include <memory>
#include <unordered_map>

class Dock
{
public:
    // 构造函数
    explicit Dock(const DockOptions &network, const std::atomic<bool> *running = nullptr)
    {
        this->registryLLM.emplace("OpenAI", std::make_unique<OpenAIStandard>(network.proxy, running));
        this->registryLLM.emplace("Anthropic", std::make_unique<AnthropicStandard>(network.proxy, running));
    }

    ChatResponse RequestChat(const ChatModel &model, const std::string &model_name, const ChatRequest &request)
    {
        ChatResponse response;
        auto LLM = this->registryLLM.find(model.api_standard);
        if (LLM == this->registryLLM.end())
        {
            LOG_ERROR("错误的API规范");
            response.code = -1;
            response.error_message = "你的服务API规范为：" + model.api_standard + "，当前并未支持";
            return response;
        }
        response = LLM->second->request_chat(model, model_name, request);
        return response;
    }

    VisionResponse RequestVision(const ChatModel &model, const std::string &model_name, const std::string &prompt, const std::string &base64)
    {
        VisionResponse response;
        auto LLM = this->registryLLM.find(model.api_standard);
        if (LLM == this->registryLLM.end())
        {
            LOG_ERROR("错误的API规范");
            response.code = -1;
            response.error_message = "你的服务API规范为：" + model.api_standard + "，当前并未支持";
            return response;
        }
        response = LLM->second->request_vision(model, model_name, prompt, base64);
        return response;
    }

    ImageResponse RequestDraw(const ChatModel &model, const std::string &model_name, const std::string &prompt)
    {
        ImageResponse response;
        auto LLM = this->registryLLM.find(model.api_standard);
        if (LLM == this->registryLLM.end())
        {
            LOG_ERROR("错误的API规范");
            response.code = -1;
            response.error_message = "你的服务API规范为：" + model.api_standard + "，当前并未支持";
            return response;
        }
        response = LLM->second->request_image(model, model_name, prompt);
        return response;
    }

    // 析构函数
    ~Dock()
    {
    }

private:
    std::unordered_map<std::string, std::unique_ptr<LLMPort>> registryLLM;
};

#endif
