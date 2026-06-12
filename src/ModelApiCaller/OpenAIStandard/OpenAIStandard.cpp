#include "OpenAIStandard.h"
#include "../Log/Log.h"
#include "../../ConfigManager/ConfigManager.h"
#include "../../Library/nlohmann/json.hpp"
#include <chrono>
#include <thread>
#include <vector>
#include <unistd.h>
#include <string>

ChatResponse OpenAIStandard::request_chat(const ChatModel &model, const std::string &model_name, const ChatRequest &request)
{
    LOG_DEBUG("使用模型:" + model_name);

    ChatResponse deserialize_result; // 反序列化response

    // 创建载荷
    nlohmann::json payload_json;
    payload_json["model"] = model_name;
    payload_json["temperature"] = request.temperature;
    payload_json["frequency_penalty"] = request.frequency_penalty;
    payload_json["presence_penalty"] = request.presence_penalty;
    nlohmann::json messages = nlohmann::json::array();

    if (!request.system_prompt.empty())
    {
        messages.push_back({{"role", "system"}, {"content", request.system_prompt}});
    }
    for (const auto &msg : request.history)
    {
        nlohmann::json m;
        m["role"] = msg.role;

        if (msg.role == "assistant" && !msg.tool_calls.empty())
        {
            // assistant 决定调工具：content 可为 null，附 tool_calls 数组
            m["content"] = msg.content.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.content);
            nlohmann::json calls = nlohmann::json::array();
            for (const auto &tc : msg.tool_calls)
            {
                calls.push_back({{"id", tc.id},
                                 {"type", "function"},
                                 {"function", {{"name", tc.name}, {"arguments", tc.arguments}}}});
            }
            m["tool_calls"] = calls;
        }
        else if (msg.role == "tool")
        {
            // 工具结果回传：必须带 tool_call_id 与对应的 assistant.tool_calls 配对
            m["content"] = msg.content;
            m["tool_call_id"] = msg.tool_call_id;
        }
        else
        {
            m["content"] = msg.content;
        }
        messages.push_back(m);
    }
    payload_json["messages"] = messages;

    // 携带可用工具（非空时）
    if (!request.tools.empty())
    {
        nlohmann::json tools_arr = nlohmann::json::array();
        for (const auto &schema : request.tools)
        {
            tools_arr.push_back(nlohmann::json::parse(schema));
        }
        payload_json["tools"] = tools_arr;
    }

    std::string payload = payload_json.dump();

    LOG_DEBUG("发送内容：" + payload);

    // 发送请求
    std::pair<std::string, long> p = this->http_post(model.endpoint, model.api_key, payload);
    if (p.second == 400)
    {
        // 可能是有不支持的参数，改为不带参数的请求方式
        LOG_WARNING("400错误，改为不带参数的请求且重新请求");
        payload_json.erase("temperature");
        payload_json.erase("frequency_penalty");
        payload_json.erase("presence_penalty");
        payload = payload_json.dump();
        p = this->http_post(model.endpoint, model.api_key, payload);
    }

    LOG_DEBUG("返回的原始消息：" + p.first);
    deserialize_result = this->chat_json_parse(p.first);
    deserialize_result.code = p.second;

    return deserialize_result;
}

VisionResponse OpenAIStandard::request_vision(const ChatModel &model, const std::string &model_name, const std::string &prompt, const std::string &base64)
{
    nlohmann::json content = nlohmann::json::array();
    content.push_back({{"type", "text"}, {"text", prompt}});
    content.push_back({{"type", "image_url"}, {"image_url", {{"url", "data:image/jpeg;base64," + base64}}}});

    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "user"}, {"content", content}});

    nlohmann::json payload_json;
    payload_json["model"] = model_name;
    payload_json["messages"] = messages;
    std::string payload = payload_json.dump();

    // 发送请求
    std::pair<std::string, long> p = this->http_post(model.endpoint, model.api_key, payload);
    LOG_DEBUG("返回的原始消息：" + p.first);
    VisionResponse deserialize_result = this->vision_json_parse(p.first);
    deserialize_result.code = p.second;
    return deserialize_result;
}

ImageResponse OpenAIStandard::request_image(const ChatModel &model, const std::string &model_name, const std::string &prompt)
{
    // 构建请求体
    nlohmann::json payload_json;
    payload_json["model"] = model_name;
    payload_json["prompt"] = prompt;
    payload_json["n"] = 1;
    payload_json["quality"] = "high";
    payload_json["size"] = "1024x1024";
    std::string payload = payload_json.dump();

    // 发送请求
    std::pair<std::string, long> p = this->http_post(model.endpoint, model.api_key, payload);
    ImageResponse deserialize_result = this->draw_json_parse(p.first);
    deserialize_result.code = p.second;
    return deserialize_result;
}

std::pair<std::string, long> OpenAIStandard::http_post(const std::string &url, const std::string &api_key, const std::string &payload)
{
    if (url.empty() || api_key.empty())
    {
        LOG_ERROR("endpoint 或 api_key 为空，无法发送请求");
        return {"", 0};
    }
    std::string endpoint = this->filterNonNormalChars(url);
    std::string key = this->filterNonNormalChars(api_key);

    // 初始化curl
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    std::string response;

    if (curl)
    {
        struct curl_slist *headers = NULL;

        std::string header_auth = "Authorization: Bearer " + key;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, header_auth.c_str());
        VerifyCertificate(curl);
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.length());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_chat);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // 可选 HTTP 代理：config.json 配置 "proxy" 非空时生效
        std::string proxy = ConfigManager::getInstance().configVariableOpt("proxy");
        if (!proxy.empty())
        {
            curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
        }

        // 执行HTTP请求
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

        // 清理资源提前：防止多路径未清理
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return {response, http_code};
    }

    LOG_ERROR("无法创建http请求...");
    return {"", 0};
}

ChatResponse OpenAIStandard::chat_json_parse(const std::string &response)
{
    if (response.empty())
    {
        return {};
    }
    try
    {
        nlohmann::json json_data = nlohmann::json::parse(response);
        ChatResponse responseFormat;

        // 获取错误信息
        if (json_data.contains("error") && json_data.is_object())
        {
            auto error = json_data["error"];
            responseFormat.error_message = error.value("message", "");
            return responseFormat;
        }

        if (json_data["choices"].is_array() && json_data["choices"].size() > 0)
        {
            for (auto &choice : json_data["choices"])
            {
                if (choice["message"].is_object())
                {
                    auto &msg = choice["message"];
                    responseFormat.content = msg.value("content", "");
                    // 过滤 <think> 内容
                    std::string &s = responseFormat.content;
                    if (s.find("<think>") == 0)
                    {
                        auto end = s.find("</think>");
                        if (end != std::string::npos)
                        {
                            s = s.erase(0, end + 8);
                        }
                    }
                    // 去除前导换行
                    while (!s.empty() && s.front() == '\n')
                    {
                        s.erase(0, 1);
                    }

                    // 解析模型的工具调用请求
                    if (msg.contains("tool_calls") && msg["tool_calls"].is_array())
                    {
                        for (auto &tc : msg["tool_calls"])
                        {
                            ResponseToolCall call;
                            call.id = tc.value("id", "");
                            if (tc.contains("function") && tc["function"].is_object())
                            {
                                call.name = tc["function"].value("name", "");
                                call.arguments = tc["function"].value("arguments", "");
                            }
                            responseFormat.tool_calls.push_back(call);
                        }
                    }
                }
                responseFormat.finish_reason = choice.value("finish_reason", "");
            }
        }
        else
        {
            responseFormat.content = "没有choices字段";
        }

        if (json_data.contains("usage") && json_data["usage"].is_object())
        {
            auto &usage = json_data["usage"];
            responseFormat.input_tokens = usage.value("prompt_tokens", 0);
            responseFormat.output_tokens = usage.value("completion_tokens", 0);
            responseFormat.total_tokens = usage.value("total_tokens", 0);
        }

        return responseFormat;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("JSON解析错误，错误内容：" + std::string(e.what()));
        return {};
    }
}

ImageResponse OpenAIStandard::draw_json_parse(const std::string &response)
{
    if (response.empty())
    {
        return {};
    }

    try
    {
        ImageResponse responseFormat;
        nlohmann::json doc = nlohmann::json::parse(response);

        // 获取错误信息
        if (doc.contains("error") && doc.is_object())
        {
            auto error = doc["error"];
            responseFormat.error_message = error.value("message", "");
            return responseFormat;
        }

        if (doc.contains("data") && doc["data"].is_array() && !doc["data"].empty())
        {
            responseFormat.image_base64 = doc["data"][0].value("b64_json", "");
            std::string prefix = responseFormat.image_base64.substr(0, 22);
            if (prefix.find("data:image/png;base64,") != std::string::npos)
            {
                responseFormat.image_base64 = responseFormat.image_base64.substr(22);
            }
        }

        if (doc.contains("usage") && doc["usage"].is_object())
        {
            const auto &data = doc["usage"];
            responseFormat.input_tokens = data.value("input_tokens", 0);
            responseFormat.output_tokens = data.value("output_tokens", 0);
            responseFormat.total_tokens = data.value("total_tokens", 0);
        }
        return responseFormat;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Json解析失败。error：" + std::string(e.what()));
        std::cerr << e.what() << '\n';
    }
    return {};
}

VisionResponse OpenAIStandard::vision_json_parse(const std::string &response)
{
    VisionResponse responseFormat;
    try
    {
        nlohmann::json doc = nlohmann::json::parse(response);

        // 获取错误信息
        if (doc.contains("error") && doc.is_object())
        {
            auto error = doc["error"];
            responseFormat.error_message = error.value("message", "");
            return responseFormat;
        }

        // 获取 choices 中的内容
        if (doc["choices"].is_array() && !doc["choices"].empty() && doc["choices"][0].is_object())
        {
            nlohmann::json choices = doc["choices"][0];
            responseFormat.finish_reason = choices.value("finish_reason", "");
            if (choices["message"].is_object())
            {
                responseFormat.content = choices["message"].value("content", "");
                responseFormat.refusal = choices["message"].value("refusal", "");
            }
        }

        // 获取 usage 中的内容
        if (doc["usage"].is_object())
        {
            nlohmann::json usage = doc["usage"];
            responseFormat.output_tokens = usage.value("completion_tokens", 0);
            responseFormat.input_tokens = usage.value("prompt_tokens", 0);
            responseFormat.total_tokens = usage.value("total_tokens", 0);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("返回的内容不是有效的Json对象。详细：" + std::string(e.what()));
        responseFormat.content = "系统提示：非法的Json";
    }
    return responseFormat;
}

// 回调函数
size_t OpenAIStandard::write_callback_chat(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    std::string *data = static_cast<std::string *>(userdata);
    size_t newLength = size * nmemb;
    try
    {
        data->append(ptr, newLength);
    }
    catch (std::bad_alloc &e)
    {
        // 内存不足异常
        LOG_ERROR("奇怪的异常，内存不足！");
        return 0;
    }
    return newLength;
}

void OpenAIStandard::VerifyCertificate(CURL *curl)
{
#if defined(__WIN32) || defined(__WIN64)
    // 设置SSL证书验证
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);   // 开启SSL证书验证
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);   // 验证证书中的主机名
    curl_easy_setopt(curl, CURLOPT_CAINFO, "cacert.pem"); // 指定CA根证书
#endif
}

// API和端点修正
std::string OpenAIStandard::filterNonNormalChars(std::string str)
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