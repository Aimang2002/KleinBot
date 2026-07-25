#include "AnthropicStandard.h"
#include "../Log/Log.h"
#include "../../Library/nlohmann/json.hpp"
#include <chrono>
#include <thread>
#include <string>

static constexpr const char *ANTHROPIC_VERSION = "2023-06-01";
static constexpr int DEFAULT_MAX_TOKENS = 4096;

ChatResponse AnthropicStandard::request_chat(const ChatModel &model, const std::string &model_name, const ChatRequest &request)
{
    ChatResponse deserialize_result;

    // 构建载荷：Anthropic 把 system 提到顶层，messages 里不再放 system 角色
    nlohmann::json payload_json;
    payload_json["model"] = model_name;
    payload_json["temperature"] = request.temperature;
    payload_json["max_tokens"] = request.max_tokens > 0 ? request.max_tokens : DEFAULT_MAX_TOKENS;

    if (!request.system_prompt.empty())
    {
        payload_json["system"] = request.system_prompt;
    }

    nlohmann::json messages = nlohmann::json::array();
    for (const auto &msg : request.history)
    {
        // Anthropic 只接受 user / assistant；system 已经提到顶层
        if (msg.role == "system")
        {
            continue;
        }
        messages.push_back({{"role", msg.role}, {"content", msg.content}});
    }
    payload_json["messages"] = messages;

    std::string payload = payload_json.dump();
    LOG_DEBUG("发送内容：" + payload);

    std::pair<std::string, long> p = this->http_post(model.endpoint, model.api_key, payload);
    LOG_DEBUG("返回的原始消息：" + p.first);

    deserialize_result = this->chat_json_parse(p.first);
    deserialize_result.code = p.second;
    return deserialize_result;
}

VisionResponse AnthropicStandard::request_vision(const ChatModel &model, const std::string &model_name, const std::string &prompt, const std::string &base64)
{
    nlohmann::json image_source;
    image_source["type"] = "base64";
    image_source["media_type"] = "image/jpeg";
    image_source["data"] = base64;

    nlohmann::json content = nlohmann::json::array();
    content.push_back({{"type", "image"}, {"source", image_source}});
    content.push_back({{"type", "text"}, {"text", prompt}});

    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "user"}, {"content", content}});

    nlohmann::json payload_json;
    payload_json["model"] = model_name;
    payload_json["max_tokens"] = DEFAULT_MAX_TOKENS;
    payload_json["messages"] = messages;
    std::string payload = payload_json.dump();

    std::pair<std::string, long> p = this->http_post(model.endpoint, model.api_key, payload);
    LOG_DEBUG("返回的原始消息：" + p.first);

    VisionResponse deserialize_result = this->vision_json_parse(p.first);
    deserialize_result.code = p.second;
    return deserialize_result;
}

ImageResponse AnthropicStandard::request_image(const ChatModel &model, const std::string &model_name, const std::string &prompt)
{
    // Anthropic 原生 API 不支持图像生成
    ImageResponse response;
    response.code = -1;
    response.error_message = "Anthropic API 不支持图像生成，请改用 OpenAI 标准（如 DALL·E）或本地 Stable Diffusion";
    LOG_WARNING("调用 AnthropicStandard::request_image 但 Anthropic 不支持图像生成");
    return response;
}

std::pair<std::string, long> AnthropicStandard::http_post(const std::string &url, const std::string &api_key, const std::string &payload)
{
    LOG_DEBUG("使用anthropic");
    if (url.empty() || api_key.empty())
    {
        LOG_ERROR("endpoint 或 api_key 为空，无法发送请求");
        return {"", 0};
    }

    std::string endpoint = this->filterNonNormalChars(url);
    std::string key = this->filterNonNormalChars(api_key);

    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    std::string response;

    if (curl)
    {
        struct curl_slist *headers = NULL;

        // Anthropic 鉴权：x-api-key + anthropic-version，不是 Bearer
        std::string header_key = "x-api-key: " + key;
        std::string header_version = std::string("anthropic-version: ") + ANTHROPIC_VERSION;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, header_key.c_str());
        headers = curl_slist_append(headers, header_version.c_str());

        VerifyCertificate(curl);
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.length());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_chat);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // 可选 HTTP 代理：config.json 配置 "proxy" 非空时生效
        if (!proxy.empty())
        {
            curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
        }

        unsigned short attempts = 5;
        long http_code = 0;
        while (attempts--)
        {
            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (res != CURLE_OK)
            {
                fprintf(stderr, "curl_easy_perform() failed: %s\n剩余请求次数：%d", curl_easy_strerror(res), attempts);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            break;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return {response, http_code};
    }

    LOG_ERROR("无法创建http请求...");
    return {"", 0};
}

ChatResponse AnthropicStandard::chat_json_parse(const std::string &response)
{
    if (response.empty())
    {
        return {};
    }
    try
    {
        nlohmann::json json_data = nlohmann::json::parse(response);
        ChatResponse responseFormat;

        // Anthropic 错误：{"type":"error","error":{"type":"...","message":"..."}}
        if (json_data.contains("error") && json_data["error"].is_object())
        {
            responseFormat.error_message = json_data["error"].value("message", "");
            return responseFormat;
        }

        // content 是 block 数组：[{type:"text", text:"..."}, ...]
        if (json_data.contains("content") && json_data["content"].is_array())
        {
            std::string text;
            for (auto &block : json_data["content"])
            {
                if (block.is_object() && block.value("type", "") == "text")
                {
                    text += block.value("text", "");
                }
            }
            responseFormat.content = text;

            // 与 OpenAIStandard 保持一致：去除前导换行
            while (!responseFormat.content.empty() && responseFormat.content.front() == '\n')
            {
                responseFormat.content.erase(0, 1);
            }
        }
        else
        {
            responseFormat.content = "没有content字段";
        }

        // 字段名是 stop_reason（end_turn / max_tokens / stop_sequence）
        responseFormat.finish_reason = json_data.value("stop_reason", "");

        if (json_data.contains("usage") && json_data["usage"].is_object())
        {
            auto &usage = json_data["usage"];
            responseFormat.input_tokens = usage.value("input_tokens", 0);
            responseFormat.output_tokens = usage.value("output_tokens", 0);
            // Anthropic 不返回 total，自己加
            responseFormat.total_tokens = responseFormat.input_tokens + responseFormat.output_tokens;
        }

        return responseFormat;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("JSON解析错误，错误内容：" + std::string(e.what()));
        return {};
    }
}

VisionResponse AnthropicStandard::vision_json_parse(const std::string &response)
{
    VisionResponse responseFormat;
    if (response.empty())
    {
        return responseFormat;
    }
    try
    {
        nlohmann::json json_data = nlohmann::json::parse(response);

        if (json_data.contains("error") && json_data["error"].is_object())
        {
            responseFormat.error_message = json_data["error"].value("message", "");
            return responseFormat;
        }

        if (json_data.contains("content") && json_data["content"].is_array())
        {
            std::string text;
            for (auto &block : json_data["content"])
            {
                if (block.is_object() && block.value("type", "") == "text")
                {
                    text += block.value("text", "");
                }
            }
            responseFormat.content = text;
        }

        responseFormat.finish_reason = json_data.value("stop_reason", "");

        if (json_data.contains("usage") && json_data["usage"].is_object())
        {
            auto &usage = json_data["usage"];
            responseFormat.input_tokens = usage.value("input_tokens", 0);
            responseFormat.output_tokens = usage.value("output_tokens", 0);
            responseFormat.total_tokens = responseFormat.input_tokens + responseFormat.output_tokens;
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("返回的内容不是有效的Json对象。详细：" + std::string(e.what()));
        responseFormat.content = "系统提示：非法的Json";
    }
    return responseFormat;
}

size_t AnthropicStandard::write_callback_chat(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    std::string *data = static_cast<std::string *>(userdata);
    size_t newLength = size * nmemb;
    try
    {
        data->append(ptr, newLength);
    }
    catch (std::bad_alloc &e)
    {
        LOG_ERROR("奇怪的异常，内存不足！");
        return 0;
    }
    return newLength;
}

void AnthropicStandard::VerifyCertificate(CURL *curl)
{
#if defined(__WIN32) || defined(__WIN64)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "cacert.pem");
#endif
}

std::string AnthropicStandard::filterNonNormalChars(std::string str)
{
    std::string result;
    for (char c : str)
    {
        if (std::isprint(c) && !std::isspace(c))
        {
            result += c;
        }
    }
    return result;
}
