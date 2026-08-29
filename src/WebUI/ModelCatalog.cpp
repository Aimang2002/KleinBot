#include "ModelCatalog.h"
#include "../../Library/nlohmann/json.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace
{
using json = nlohmann::json;

struct BodyBuffer
{
    std::string content;
    std::size_t limit = 0;
    bool exceeded = false;
};

std::size_t appendBody(char *pointer, std::size_t size, std::size_t count, void *userData)
{
    auto *buffer = static_cast<BodyBuffer *>(userData);
    const std::size_t total = size * count;
    if (buffer->content.size() + total > buffer->limit)
    {
        buffer->exceeded = true;
        return 0;
    }
    buffer->content.append(pointer, total);
    return total;
}

std::string trimmedSnippet(const std::string &text, std::size_t limit)
{
    std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    std::size_t end = text.find_last_not_of(" \t\r\n");
    std::string snippet = text.substr(begin, end - begin + 1);
    if (snippet.size() > limit)
        snippet = snippet.substr(0, limit) + "…";
    return snippet;
}

std::string lowered(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

struct HttpResult
{
    CURLcode code = CURLE_OK;
    long statusCode = 0;
    std::string body;
    bool exceeded = false;
};

// 共享的 curl 交换：headers 由调用方构造（鉴权头因 API 标准而异），requestBody 为空即 GET
HttpResult httpExchange(const std::string &url, curl_slist *headers,
                        const std::string *requestBody, long timeoutMs)
{
    static const bool curlReady = [] {
        return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }();

    HttpResult result;
    if (!curlReady)
    {
        result.code = CURLE_FAILED_INIT;
        return result;
    }

    CURL *curl = curl_easy_init();
    if (curl == nullptr)
    {
        result.code = CURLE_FAILED_INIT;
        return result;
    }

    BodyBuffer buffer;
    buffer.limit = 2 * 1024 * 1024;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 4000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    if (requestBody != nullptr)
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody->c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(requestBody->size()));
    }

    result.code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.statusCode);
    curl_easy_cleanup(curl);

    result.body = std::move(buffer.content);
    result.exceeded = buffer.exceeded;
    return result;
}

curl_slist *baseHeaders(const std::string &apiKey, const std::string &apiStandard,
                        const char *extraAccept)
{
    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, extraAccept != nullptr ? extraAccept : "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (compatible; KleinBot/2.4)");
    if (apiStandard == "Anthropic")
    {
        headers = curl_slist_append(headers, ("x-api-key: " + apiKey).c_str());
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    }
    else
    {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + apiKey).c_str());
    }
    return headers;
}
}

namespace ModelCatalog
{
std::optional<std::string> deriveModelsUrl(const std::string &chatEndpoint)
{
    if (chatEndpoint.rfind("http://", 0) != 0 && chatEndpoint.rfind("https://", 0) != 0)
        return std::nullopt;

    std::string endpoint = chatEndpoint;
    while (endpoint.size() > 1 && endpoint.back() == '/')
        endpoint.pop_back();

    if (endpoint.size() >= 7 && endpoint.compare(endpoint.size() - 7, 7, "/models") == 0)
        return endpoint;

    // 注册表存的是完整对话地址；列表接口与对话接口同属一个 base 段
    for (const char *suffix : {"/chat/completions", "/messages"})
    {
        const std::string pattern = suffix;
        if (endpoint.size() > pattern.size() &&
            endpoint.compare(endpoint.size() - pattern.size(), pattern.size(), pattern) == 0)
            return endpoint.substr(0, endpoint.size() - pattern.size()) + "/models";
    }
    return std::nullopt;
}

std::vector<std::string> parseModelsResponse(const std::string &body, std::string &error)
{
    nlohmann::json document;
    try
    {
        document = nlohmann::json::parse(body);
    }
    catch (const std::exception &)
    {
        error = "端点响应不是有效 JSON";
        return {};
    }

    const nlohmann::json *data = nullptr;
    if (document.is_object() && document.contains("data") && document["data"].is_array())
        data = &document["data"];
    else if (document.is_array())
        data = &document;
    if (data == nullptr)
    {
        error = "端点响应缺少模型列表（data 数组）";
        return {};
    }

    std::set<std::string> unique;
    for (const nlohmann::json &item : *data)
    {
        std::string name;
        if (item.is_string())
            name = item.get<std::string>();
        else if (item.is_object())
        {
            if (item.contains("id") && item["id"].is_string())
                name = item["id"].get<std::string>();
            else if (item.contains("name") && item["name"].is_string())
                name = item["name"].get<std::string>();
        }
        if (!name.empty())
            unique.insert(name);
    }
    return std::vector<std::string>(unique.begin(), unique.end());
}

std::optional<std::vector<std::string>> fetch(const std::string &modelsUrl,
                                              const std::string &apiKey,
                                              const std::string &apiStandard,
                                              long timeoutMs,
                                              std::string &error)
{
    curl_slist *headers = baseHeaders(apiKey, apiStandard, nullptr);
    const HttpResult result = httpExchange(modelsUrl, headers, nullptr, timeoutMs);
    curl_slist_free_all(headers);

    if (result.code == CURLE_OPERATION_TIMEDOUT)
    {
        error = "请求超时，请检查端点是否可达";
        return std::nullopt;
    }
    if (result.code != CURLE_OK)
    {
        error = std::string("请求失败：") + curl_easy_strerror(result.code);
        return std::nullopt;
    }
    if (result.exceeded)
    {
        error = "端点响应超过大小限制";
        return std::nullopt;
    }
    if (result.statusCode != 200)
    {
        error = "端点返回状态码 " + std::to_string(result.statusCode);
        const std::string snippet = trimmedSnippet(result.body, 200);
        if (!snippet.empty())
            error += "：" + snippet;
        return std::nullopt;
    }

    return parseModelsResponse(result.body, error);
}

VisionProbe classifyVisionProbe(long statusCode, const std::string &body)
{
    if (statusCode >= 200 && statusCode < 300)
        return VisionProbe::Vision;
    if (statusCode >= 400 && statusCode < 500)
    {
        const std::string loweredBody = lowered(body);
        // 允许值枚举里同时出现 image（如“取值范围 ['text','image_url']”）：说明图片形态本身
        // 被接受，这次 400 另有原因（多半是请求形态问题），不能据此判“不支持”
        static const char *const enumerations[] = {
            "取值范围", "allowed", "must be one of", "expected one of", "valid values"};
        const bool enumeratesAllowedValues =
            std::any_of(std::begin(enumerations), std::end(enumerations),
                        [&loweredBody](const char *marker) {
                            return loweredBody.find(marker) != std::string::npos;
                        });
        if (enumeratesAllowedValues && loweredBody.find("image") != std::string::npos)
            return VisionProbe::Unknown;

        // 4xx 且报错指向图片/多模态/内容形态不支持 → 结论“不支持”；
        // 含中文供应商的报错形态（如智谱“取值范围 ['text']”、“不支持图片”）。
        // 其它 4xx（模型不存在、鉴权、参数、限流等）保持无法判定
        static const char *const markers[] = {
            "image", "vision", "multimodal", "multi-modal", "modal",
            "content_type", "content type", "contentblock", "content block",
            "not support", "unsupported", "does not support",
            "['text']", "content.type 参数非法", "不支持"};
        for (const char *marker : markers)
        {
            if (loweredBody.find(marker) != std::string::npos)
                return VisionProbe::NoVision;
        }
    }
    return VisionProbe::Unknown;
}

std::optional<VisionProbe> probeVision(const std::string &chatEndpoint,
                                       const std::string &apiKey,
                                       const std::string &apiStandard,
                                       const std::string &modelName,
                                       long timeoutMs,
                                       std::string &error)
{
    // 1x1 像素 PNG：探测载荷固定且极小
    static const char *const tinyPng =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=";

    nlohmann::json payload;
    if (apiStandard == "Anthropic")
    {
        payload = {{"model", modelName},
                   {"max_tokens", 16},
                   {"messages", json::array({{
                       {"role", "user"},
                       {"content", json::array({
                           {{"type", "image"},
                            {"source", {{"type", "base64"},
                                        {"media_type", "image/png"},
                                        {"data", tinyPng}}}},
                           {{"type", "text"}, {"text", "Reply with OK"}},
                       })},
                   }})}};
    }
    else
    {
        payload = {{"model", modelName},
                   {"max_tokens", 16},
                   {"messages", json::array({{
                       {"role", "user"},
                       {"content", json::array({
                           {{"type", "text"}, {"text", "Reply with OK"}},
                           {{"type", "image_url"},
                            {"image_url", {{"url", "data:image/png;base64," + std::string(tinyPng)}}}},
                       })},
                   }})}};
    }
    const std::string requestBody = payload.dump();

    curl_slist *headers = baseHeaders(apiKey, apiStandard, "Content-Type: application/json");
    const HttpResult result = httpExchange(chatEndpoint, headers, &requestBody, timeoutMs);
    curl_slist_free_all(headers);

    if (result.code == CURLE_OPERATION_TIMEDOUT)
        error = "探测请求超时";
    else if (result.code != CURLE_OK)
        error = std::string("探测请求失败：") + curl_easy_strerror(result.code);
    if (result.code != CURLE_OK)
        return std::nullopt;

    if (result.statusCode >= 200 && result.statusCode < 300)
        return VisionProbe::Vision;
    const VisionProbe outcome = classifyVisionProbe(result.statusCode, result.body);
    if (outcome == VisionProbe::Unknown)
    {
        // 无法判定时把上游状态带回，面板向用户展示原因
        error = "端点返回状态码 " + std::to_string(result.statusCode);
        const std::string snippet = trimmedSnippet(result.body, 200);
        if (!snippet.empty())
            error += "：" + snippet;
    }
    return outcome;
}
}
