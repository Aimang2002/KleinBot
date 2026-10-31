#ifndef MODEL_ENDPOINT_OPTIONS_H
#define MODEL_ENDPOINT_OPTIONS_H

#include <functional>
#include <string>
#include <utility>

struct ModelEndpointOptions
{
    std::string model;
    std::string endpoint;
    std::string apiKey;
    std::string apiStandard;

    bool configured() const
    {
        return !model.empty() && !endpoint.empty() && !apiStandard.empty();
    }
};

// 端点未就绪时的自然语言说明（回灌给模型，由模型转述给用户）；就绪返回空串。
// feature 如 "生图"/"视觉"，configPath 如 "models.drawing"，用于把原因定位到具体配置位置。
inline std::string modelEndpointReadinessText(const ModelEndpointOptions &endpoint,
                                              const std::string &feature, const std::string &configPath)
{
    if (!endpoint.configured())
        return feature + "功能当前不可用：管理员尚未配置" + feature + "模型（" + configPath +
               " 缺少模型名、接口地址或 API 标准）。请向用户说明暂时无法使用该功能，"
               "需要管理员补全配置后才能启用。";
    if (endpoint.apiKey.empty())
        return feature + "功能当前不可用：" + feature + "模型已配置，但 API Key 为空（" + configPath +
               " 的密钥未填写或环境变量缺失），接口会拒绝调用。请向用户说明暂时无法使用该功能，"
               "需要管理员补全密钥后才能启用。";
    return {};
}

// 调用期端点来源（ADR-022）：组合根构造（见 ModelEndpointResolver.h 的 registryEndpoint），
// 使用方（工具 / 命令 / Message）只依赖这个类型，不依赖注册表
using EndpointProvider = std::function<ModelEndpointOptions()>;

// 固定端点（测试与不需要注册表解析的场景）
inline EndpointProvider fixedEndpoint(ModelEndpointOptions options)
{
    return [options = std::move(options)]() { return options; };
}

// 空 provider 视为未配置
inline ModelEndpointOptions currentEndpoint(const EndpointProvider &provider)
{
    return provider ? provider() : ModelEndpointOptions{};
}

#endif
