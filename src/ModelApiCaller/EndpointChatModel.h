#ifndef ENDPOINT_CHAT_MODEL_H
#define ENDPOINT_CHAT_MODEL_H

// 端点配置 → Dock/LLMPort 所需的 ChatModel（只映射连接字段；模型名单独作 model_name 传）。
// 独立成头文件，Dock 只依赖它，不经由 ModelEndpointResolver 间接依赖 ModelRegistry
#include "ModelEndpointOptions.h"
#include "../ModelRegistry/ChatModel.h"

inline ChatModel toChatModel(const ModelEndpointOptions &options)
{
    ChatModel model;
    model.endpoint = options.endpoint;
    model.api_key = options.apiKey;
    model.api_standard = options.apiStandard;
    return model;
}

#endif // ENDPOINT_CHAT_MODEL_H
