#ifndef MODEL_ENDPOINT_RESOLVER_H
#define MODEL_ENDPOINT_RESOLVER_H

/*
 * 专用模型端点（models.vision / models.drawing / models.worker）的唯一解析点。
 *
 * 两种配置形态：
 *   - 全配形态：model + endpoint + api_standard (+ api_key)，直连；
 *   - 注册表形态：只填 model，端点/密钥/API 标准在调用时按名字从 ModelRegistry
 *     解析（注册表热重载即时生效）。名字未注册时原样返回（端点为空），由调用方的
 *     就绪检查（modelEndpointReadinessText）或回退逻辑处理。
 * 调用方不要冻结解析结果：需要随注册表热重载的地方持有 EndpointProvider，
 * 每次调用时取一次。
 */
#include "ModelEndpointOptions.h"
#include "../ModelRegistry/ChatModel.h"
#include "../ModelRegistry/ModelRegistry.h"

#include <functional>
#include <optional>
#include <utility>

// 端点 → Dock/LLMPort 所需的 ChatModel（只映射连接字段；模型名单独作 model_name 传）
inline ChatModel toChatModel(const ModelEndpointOptions &options)
{
    ChatModel model;
    model.endpoint = options.endpoint;
    model.api_key = options.apiKey;
    model.api_standard = options.apiStandard;
    return model;
}

// 注册表形态解析：endpoint 为空且 model 非空、且注册表找得到该名字时，用注册条目
// 补齐 endpoint/apiKey/apiStandard；其余情况原样返回
inline ModelEndpointOptions resolveEndpoint(const ModelEndpointOptions &configured,
                                            const ModelRegistry &registry)
{
    ModelEndpointOptions resolved = configured;
    if (resolved.endpoint.empty() && !resolved.model.empty())
    {
        if (const std::optional<ChatModel> found = registry.find(resolved.model))
        {
            resolved.endpoint = found->endpoint;
            resolved.apiKey = found->api_key;
            resolved.apiStandard = found->api_standard;
        }
    }
    return resolved;
}

// 调用期端点来源：组合根构造，使用方每次调用时取一次
using EndpointProvider = std::function<ModelEndpointOptions()>;

// 固定端点（测试与不需要注册表解析的场景）
inline EndpointProvider fixedEndpoint(ModelEndpointOptions options)
{
    return [options = std::move(options)]() { return options; };
}

// 按注册表在调用时解析；registry 必须比返回的 provider 活得久（组合根栈对象）
inline EndpointProvider registryEndpoint(ModelEndpointOptions configured, const ModelRegistry &registry)
{
    return [configured = std::move(configured), &registry]()
    { return resolveEndpoint(configured, registry); };
}

// 空 provider 视为未配置
inline ModelEndpointOptions currentEndpoint(const EndpointProvider &provider)
{
    return provider ? provider() : ModelEndpointOptions{};
}

#endif // MODEL_ENDPOINT_RESOLVER_H
