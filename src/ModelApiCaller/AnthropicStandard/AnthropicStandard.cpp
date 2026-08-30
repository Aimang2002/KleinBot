#include "AnthropicStandard.h"
#include "../ChatPayloadBuilder.h"
#include "../Log/Log.h"
#include "../../Network/CurlRequestControl.h"
#include "../../Library/nlohmann/json.hpp"
#include <chrono>
#include <thread>
#include <string>

static constexpr const char *ANTHROPIC_VERSION = "2023-06-01";
static constexpr int DEFAULT_MAX_TOKENS = 4096;

ChatResponse AnthropicStandard::request_chat(const ChatModel &model, const std::string &model_name, const ChatRequest &request)
{
    ChatResponse deserialize_result;
    if (CurlRequestControl::cancellationRequested(running))
    {
        deserialize_result.cancelled = true;
        return deserialize_result;
    }

    nlohmann::json payload_json = ChatPayloadBuilder::anthropic(
        model_name, request, DEFAULT_MAX_TOKENS);
    const bool multimodalRequest = ChatPayloadBuilder::imageCount(request) > 0;
    std::string payload = payload_json.dump();
    LOG_DEBUG("发送模型请求：消息数 " + std::to_string(request.history.size()) +
              "，图片数 " + std::to_string(ChatPayloadBuilder::imageCount(request)));

    std::pair<std::string, long> p = this->http_post(model.endpoint, model.api_key, payload);
    if (CurlRequestControl::cancellationRequested(running))
    {
        deserialize_result.cancelled = true;
        return deserialize_result;
    }
    LOG_DEBUG("返回的原始消息：" + p.first);

    deserialize_result = this->chat_json_parse(p.first);
    deserialize_result.code = p.second;
    deserialize_result.multimodal_unsupported = multimodalRequest &&
        ChatPayloadBuilder::explicitlyRejectsMultimodal(p.second, p.first);
    return deserialize_result;
}

VisionResponse AnthropicStandard::request_vision(const ChatModel &model, const std::string &model_name, const std::string &prompt, const std::string &base64)
{
    if (CurlRequestControl::cancellationRequested(running))
    {
        VisionResponse response;
        response.cancelled = true;
        return response;
    }
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
    if (CurlRequestControl::cancellationRequested(running))
    {
        VisionResponse response;
        response.cancelled = true;
        return response;
    }
    LOG_DEBUG("返回的原始消息：" + p.first);

    VisionResponse deserialize_result = this->vision_json_parse(p.first);
    deserialize_result.code = p.second;
    return deserialize_result;
}

ImageResponse AnthropicStandard::request_image(const ChatModel &model, const std::string &model_name, const std::string &prompt)
{
    if (CurlRequestControl::cancellationRequested(running))
    {
        ImageResponse response;
        response.cancelled = true;
        return response;
    }
    // Anthropic 原生 API 不支持图像生成
    ImageResponse response;
    response.code = -1;
    response.error_message = "Anthropic API 不支持图像生成，请改用 OpenAI 标准的图像接口";
    LOG_WARNING("调用 AnthropicStandard::request_image 但 Anthropic 不支持图像生成");
    return response;
}

std::pair<std::string, long> AnthropicStandard::http_post(const std::string &url, const std::string &api_key, const std::string &payload)
{
    LOG_DEBUG("使用anthropic");
    if (url.empty() || api_key.empty())
    {
        LOG_ERROR("endpoint 或 api_key 为空，无法发送请求");
        return {nlohmann::json{{"error", {{"message", "模型服务配置不完整，请联系管理员。"}}}}.dump(), 500};
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

        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.length());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_chat);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        CurlRequestControl::configure(curl, running);

        // 可选 HTTP 代理：.config.json 配置 "proxy" 非空时生效
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
            if (CurlRequestControl::wasCancelled(res, running))
            {
                LOG_INFO("模型请求已因程序停止而取消");
                break;
            }
            if (res != CURLE_OK)
            {
                LOG_WARNING("模型请求失败：" + std::string(curl_easy_strerror(res)));
                if (res == CURLE_OPERATION_TIMEDOUT)
                    break;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            break;
        }

        if (res != CURLE_OK && !CurlRequestControl::wasCancelled(res, running))
        {
            http_code = CurlRequestControl::failureStatusCode(res);
            response = nlohmann::json{
                {"error", {{"message", CurlRequestControl::failureMessage(res)}}}
            }.dump();
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return {response, http_code};
    }

    LOG_ERROR("无法创建http请求...");
    return {nlohmann::json{{"error", {{"message", "无法初始化模型网络请求。"}}}}.dump(), 503};
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

        // content 是 block 数组：[{type:"text", text:"..."}, {type:"tool_use", ...}]
        if (json_data.contains("content") && json_data["content"].is_array())
        {
            std::string text;
            for (auto &block : json_data["content"])
            {
                if (!block.is_object())
                    continue;
                const std::string type = block.value("type", "");
                if (type == "text")
                {
                    text += block.value("text", "");
                }
                else if (type == "tool_use")
                {
                    ResponseToolCall call;
                    call.id = block.value("id", "");
                    call.name = block.value("name", "");
                    call.arguments = block.value("input", nlohmann::json::object()).dump();
                    responseFormat.tool_calls.push_back(std::move(call));
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

        // 字段名是 stop_reason（end_turn / max_tokens / stop_sequence / tool_use）；
        // tool_use 映射为 ChatService 工具循环使用的 tool_calls
        responseFormat.finish_reason = json_data.value("stop_reason", "");
        if (responseFormat.finish_reason == "tool_use")
            responseFormat.finish_reason = "tool_calls";

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
