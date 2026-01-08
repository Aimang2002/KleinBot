#include "OpenAIStandard.h"
#include "../Log/Log.h"
#include "../../Library/nlohmann/json.hpp"
#include <chrono>
#include <thread>
#include <vector>
#include <unistd.h>
#include <string>

OpenAIChatResponse OpenAIStandard::send_to_chat(const std::string endpoint, std::string api_key, const nlohmann::json &body)
{
    /*
     * data参数里必须构建好Json数据以及prompt，本函数不提供Json数据封装
     */

#ifdef DEBUG
    LOG_INFO("使用模型:" + body.value("model", ""));
#endif

    // 初始化curl
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();

    if (curl)
    {
        struct curl_slist *headers = NULL;

        std::string header_auth = "Authorization: Bearer " + this->filterNonNormalChars(api_key);
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, header_auth.c_str());

        std::string message = body.dump();
        LOG_DEBUG("发送内容：" + message);
        std::string response;
        VerifyCertificate(curl);
        curl_easy_setopt(curl, CURLOPT_URL, this->filterNonNormalChars(endpoint).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, message.length());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_chat);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // 执行HTTP请求
        unsigned short request = 5;
        long http_code = 0;
        while (request--)
        {
            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (res != CURLE_OK)
            {
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            break;
        }

#ifdef DEBUG
        std::cout << "OpenAI 原始消息：" << response << std::endl;
#endif
        // 清理资源提前：防止多路径未清理
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        // if (http_code >= 400)
        // {
        //     OpenAIChatResponse tmp = this->chat_json_parse(response);
        //     tmp.code = http_code;
        //     tmp.choices_message_content = "系统提示：无法将问题发送给OpenAI，请稍后再重试或联系管理员...";
        //     return tmp;
        // }

        // 响应格式化
        OpenAIChatResponse responseFormat = this->chat_json_parse(response);
        responseFormat.code = http_code;
        // 无误返回
        return responseFormat;
    }

    LOG_ERROR("无法创建http请求...");
    return {};
}

// 调用视觉模型
OpenAIVisionResponse OpenAIStandard::send_to_vision(const std::string endpoint, const std::string api_key, std::string model, const std::string &prompt, const std::string &base64)
{
    nlohmann::json content = nlohmann::json::array();
    content.push_back({{"type", "text"}, {"text", prompt}});
    content.push_back({{"type", "image_url"},
                       {"image_url", {{"url", "data:image/jpeg;base64," + base64}}}});

    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "user"}, {"content", content}});

    nlohmann::json payload = {
        {"model", model},
        {"messages", messages}};

    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();

    if (curl)
    {
        // 设置API密钥
        struct curl_slist *headers = NULL;
        VerifyCertificate(curl);
        std::string header_auth = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, header_auth.c_str());

        // 设置请求数据，包括上下文
        nlohmann::json payload_send_char_message = {
            {"model", model},
            {"messages", messages}};

        std::string message = payload_send_char_message.dump();
        std::string response;
        // 封装HTTP POST数据报 + 设置libcurl选项
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, message.length());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_chat);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        OpenAIVisionResponse responseFormat;
        long http_code;

        // 执行HTTP请求
        unsigned short request = 5;
        while (request--)
        {
            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (res != CURLE_OK)
            {
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            break;
        }
        if (request < 1)
        {
            responseFormat.choice_message_content = "系统提示：无法将问题发送给OpenAI，请稍后再重试或联系管理员...";
            LOG_ERROR("无法请求...");
            return responseFormat;
        }

        // 清理资源
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
#ifdef DEBUG
        std::cout << "OpenAI 返回的原始消息：" << response << std::endl;
#endif
        // 响应数据格式化
        responseFormat = this->vision_json_parse(response);
        responseFormat.code = http_code;
        return responseFormat;
    }

    OpenAIVisionResponse tmp;
    tmp.choice_message_content = "系统提示：无法跟OpenAI连接，请联系管理员...";
    return tmp;
}

// 调用绘图模型
OpenAIImageResponse OpenAIStandard::send_to_draw(const std::string endpoint, const std::string api_key, std::string model, const std::string &prompt)
{
    CURL *curl;
    CURLcode res;

    if (endpoint.empty() || api_key.empty())
    {
        LOG_ERROR("endpoint 或 api_key 为空，无法发送请求");
        OpenAIImageResponse errorResponse;
        errorResponse.error_message = "系统提示：endpoint或api_key参数无效";
        errorResponse.code = 400;
        return errorResponse;
    }

    curl = curl_easy_init();
    if (curl)
    {
        // 设置请求URL
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());

        // 设置请求头
        struct curl_slist *headers = NULL;
        VerifyCertificate(curl);
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string authorization = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, authorization.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // 构造请求体
        nlohmann::json postData;
        postData["model"] = model;
        postData["prompt"] = prompt;
        postData["n"] = 1;
        postData["quality"] = "hd";
        postData["user"] = "string";
        postData["size"] = "1024x1024";

        std::string payload = postData.dump();

        LOG_DEBUG(payload);

        // curl_easy_setopt 如果直接传 c_str()，生命周期需要保证，故用变量存储
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());

        // 设置响应回调函数
        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_chat);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // 执行HTTP请求
        res = curl_easy_perform(curl);
        long http_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (res != CURLE_OK)
        {
            LOG_ERROR(curl_easy_strerror(res));
        }
        else
        {
            // LOG_DEBUG(prompt);
        }

#ifdef DEBUG
        LOG_DEBUG("原始内容：" + response);
#endif

        // 清理
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        // 解析内容
        OpenAIImageResponse responseFormat = this->draw_json_parse(response);
        responseFormat.code = http_code;
        return responseFormat;
    }
    OpenAIImageResponse errorResponse;
    errorResponse.error_message = "系统提示：无法初始化CURL，请联系管理员...";
    errorResponse.code = 500;
    return errorResponse;
}

OpenAIChatResponse OpenAIStandard::chat_json_parse(const std::string &response)
{
    if (response.empty())
    {
        return {};
    }
    try
    {
        nlohmann::json json_data = nlohmann::json::parse(response);
        OpenAIChatResponse responseFormat;

        // 获取错误信息
        if (json_data.contains("error") && json_data.is_object())
        {
            auto error = json_data["error"];
            responseFormat.error_message = error.value("message", "");
            responseFormat.error_type = error.value("type", "");
            return responseFormat;
        }

        responseFormat.id = json_data.value("id", "");
        responseFormat.model = json_data.value("model", "");
        responseFormat.object = json_data.value("object", "");
        responseFormat.created = json_data.value("created", 0);

        if (json_data["choices"].is_array() && json_data["choices"].size() > 0)
        {
            for (auto &choice : json_data["choices"])
            {
                responseFormat.choices_index = choice.value("index", -1);
                if (choice["message"].is_object())
                {
                    auto &msg = choice["message"];
                    responseFormat.choices_message_role = msg.value("role", "");
                    responseFormat.choices_message_content = msg.value("content", "");
                }
                responseFormat.choices_finish_reason = choice.value("finish_reason", "");
            }
        }
        else
        {
            responseFormat.choices_message_content = "没有choices字段";
        }

        if (json_data.contains("usage") && json_data["usage"].is_object())
        {
            auto &usage = json_data["usage"];
            responseFormat.usage_prompt_tokens = usage.value("prompt_tokens", 0);
            responseFormat.usage_completion_tokens = usage.value("completion_tokens", 0);
            responseFormat.usage_total_tokens = usage.value("total_tokens", 0);
        }

        return responseFormat;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("JSON解析错误，错误内容：" + std::string(e.what()));
        return {};
    }
}

OpenAIImageResponse OpenAIStandard::draw_json_parse(const std::string &response)
{
    if (response.empty())
    {
        return {};
    }

    try
    {
        OpenAIImageResponse responseFormat;
        nlohmann::json doc = nlohmann::json::parse(response);

        // 获取错误信息
        if (doc.contains("error") && doc.is_object())
        {
            auto error = doc["error"];
            responseFormat.error_message = error.value("message", "");
            responseFormat.error_type = error.value("type", "");
            return responseFormat;
        }

        responseFormat.created = doc.value("created", 0);
        responseFormat.output_format = doc.value("output_format", "");
        responseFormat.quality = doc.value("quality", "");
        responseFormat.size = doc.value("size", "");
        if (doc.contains("data") && doc["data"].is_array() && !doc["data"].empty())
        {
            responseFormat.data_base64 = doc["data"][0].value("b64_json", "");
            std::string prefix = responseFormat.data_base64.substr(0, 22);
            if (prefix.find("data:image/png;base64,") != std::string::npos)
            {
                responseFormat.data_base64 = responseFormat.data_base64.substr(22);
            }
        }

        if (doc.contains("usage") && doc["usage"].is_object())
        {
            const auto &data = doc["usage"];
            responseFormat.usage_input_tokens = data.value("input_tokens", 0);
            responseFormat.usage_output_tokens = data.value("output_tokens", 0);
            responseFormat.usage_total_tokens = data.value("total_tokens", 0);
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

OpenAIVisionResponse OpenAIStandard::vision_json_parse(const std::string &response)
{
    OpenAIVisionResponse responseFormat;
    try
    {
        nlohmann::json doc = nlohmann::json::parse(response);

        // 获取错误信息
        if (doc.contains("error") && doc.is_object())
        {
            auto error = doc["error"];
            responseFormat.error_message = error.value("message", "");
            responseFormat.error_type = error.value("type", "");
            return responseFormat;
        }

        responseFormat.id = doc.value("id", "");
        responseFormat.model = doc.value("model", "");
        responseFormat.created = doc.value("created", -1);

        // 获取 choices 中的内容
        if (doc["choices"].is_array() && !doc["choices"].empty() && doc["choices"][0].is_object())
        {
            nlohmann::json choices = doc["choices"][0];
            responseFormat.finish_reason = choices.value("finish_reason", "");
            if (choices["message"].is_object())
            {
                responseFormat.choice_message_content = choices["message"].value("content", "");
                responseFormat.choice_message_refusal = choices["message"].value("refusal", "");
            }
        }

        // 获取 usage 中的内容
        if (doc["usage"].is_object())
        {
            nlohmann::json usage = doc["usage"];
            responseFormat.usage_completion_tokens = usage.value("completion_tokens", 0);
            responseFormat.usage_prompt_tokens = usage.value("prompt_tokens", 0);
            responseFormat.usage_total_tokens = usage.value("total_tokens", 0);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("返回的内容不是有效的Json对象。详细：" + std::string(e.what()));
        responseFormat.choice_message_content = "系统提示：非法的Json";
    }
    return responseFormat;
}

// 回调函数
size_t OpenAIStandard::write_callback_chat(char *ptr, size_t size, size_t nmemb, std::string *userdata)
{
    size_t newLength = size * nmemb;
    try
    {
        userdata->append(ptr, newLength);
    }
    catch (std::bad_alloc &e)
    {
        // 内存不足异常
        LOG_ERROR("奇怪的异常，内存不足！");
        return 0;
    }
    return newLength;
}
// Key 错误判断
bool OpenAIStandard::isKeyError(std::string &message)
{
    if (message.find("无效的令牌") != message.npos)
    {
        LOG_ERROR(message);
        message = "系统提示：Key 有误！";
#ifdef DEBUG
        LOG_ERROR("Key有误！");
#endif
        return true;
    }
    else if (message.find("该令牌额度已用尽") != message.npos)
    {
        LOG_ERROR(message);
        message = "系统提示：Key额度用完！请联系管理员...";
#ifdef DEBUG
        LOG_ERROR("Key额度已用完！");
#endif
        return true;
    }
    return false;
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