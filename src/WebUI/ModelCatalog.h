#ifndef MODEL_CATALOG_H
#define MODEL_CATALOG_H

#include <optional>
#include <string>
#include <vector>

// 模型目录拉取：OpenAI 兼容与 Anthropic 原生接口都提供标准的模型列表接口
// （GET {base}/models，响应同为 {"data":[{"id":...}]}），面板据此把模型名
// 从手填改为从端点实际可用列表中勾选。

namespace ModelCatalog
{
// 从对话端点推导模型列表地址：去掉结尾的 /chat/completions 或 /messages 段拼上
// /models；端点本身已是 /models 结尾时原样返回；scheme 非 http(s) 或推导不出时为空
std::optional<std::string> deriveModelsUrl(const std::string &chatEndpoint);

// 解析列表响应，返回去重排序后的模型 id；兼容直接返回字符串数组的实现。
// 解析失败时置 error 并返回空
std::vector<std::string> parseModelsResponse(const std::string &body, std::string &error);

// 用 curl 拉取模型列表；apiStandard 决定鉴权头形态（OpenAI: Bearer，Anthropic:
// x-api-key + anthropic-version）。失败时置 error 并返回空
std::optional<std::vector<std::string>> fetch(const std::string &modelsUrl,
                                              const std::string &apiKey,
                                              const std::string &apiStandard,
                                              long timeoutMs,
                                              std::string &error);

// 视觉探测结论：Vision=支持（2xx）；NoVision=明确不支持（4xx 且报错指向图片/多模态）；
// Unknown=无法判定（网络/鉴权/限流/其它错误——不得当成“不支持”）
enum class VisionProbe
{
    Vision,
    NoVision,
    Unknown
};

// 纯分类，便于单测：状态码 + 响应体 → 三态结论
VisionProbe classifyVisionProbe(long statusCode, const std::string &body);

// 向指定模型发一条 1x1 图片 + 极短文字的多模态请求，按响应分类视觉能力。
// endpoint 即注册表中的完整对话地址；失败（网络层）置 error 并返回空
std::optional<VisionProbe> probeVision(const std::string &chatEndpoint,
                                       const std::string &apiKey,
                                       const std::string &apiStandard,
                                       const std::string &modelName,
                                       long timeoutMs,
                                       std::string &error);
}

#endif
