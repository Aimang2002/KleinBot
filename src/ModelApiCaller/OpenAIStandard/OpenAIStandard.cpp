#include "OpenAIStandard.h"
#include "../Log/Log.h"
#include "../JsonParse/JsonParse.h"
#include <chrono>
#include <thread>
#include <vector>
#include <unistd.h>
#include <string>

// 文本翻译
bool OpenAIStandard::text_translate(std::string &text, const std::string model, std::string language, std::string endpoint, std::string api_key)
{
    LOG_INFO("使用了文本翻译");
    // 调整格式
    std::string ss;
    if (language == "EN")
    {
        ss = R"({"model":")" + model + "\",\"messages\":[";
        ss += R"({"role": "system", "content": "You will be provided with a sentence in English, and your task is to translate it into English."},
        {"role": "assistant", "content": "OK"},
        {"role": "user", "content": ")" +
              text + "\"}]";
        ss += R"(,"temperature":0.1,"top_p":0.9,"frequency_penalty":0,"presence_penalty":0)" + std::string("}"); // 超参数
    }
    if (language == "ZH")
    {
        ss = R"("model":")" + model + "\",\"messages\":[";
        ss += R"([
        {"role": "system", "content": "翻译成中文"},
        {"role": "assistant", "content": "好的"},
        {"role": "user", "content": ")";
        ss += text + "\"}]";
        ss += R"(,"temperature":0.1,"top_p":0.9,"frequency_penalty":0,"presence_penalty":0)" + std::string("}");
    }

    text = ss;

    send_to_chat(text, endpoint, api_key);

    // json解析
    if (text.find("choices") != text.npos)
    {
        text = text.substr(text.find("content") + 10); // 删除前缀
        text = text.substr(0, text.find("}") - 1);
    }
    else
    {
        LOG_ERROR("翻译失败！错误消息：" + text);
        return false;
    }

    return true;
}

OpenAIChatResponse OpenAIStandard::send_to_chat(const nlohmann::json &body, std::string endpoint, std::string api_key)
{
    /*
     * data参数里必须构建好Json数据以及prompt，本函数不提供Json数据封装
     */

#ifdef DEBUG
    LOG_INFO("使用模型:" + body.value("model", ""));
#endif

    // api&endpoint纠正
    endpoint = this->filterNonNormalChars(endpoint);
    api_key = this->filterNonNormalChars(api_key);

    // 初始化curl
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();

    if (curl)
    {
        struct curl_slist *headers = NULL;

        std::string header_auth = "Authorization: Bearer " + api_key;
        //+api_key;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, header_auth.c_str());

        LOG_DEBUG("发送内容：" + body);
        // std::cout << "发送内容：" << body << std::endl;
        std::string response;
        VerifyCertificate(curl);
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str()); // 添加端点
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.dump().c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_chat);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // 执行HTTP请求
        unsigned short request = 5;
        while (request--)
        {
            res = curl_easy_perform(curl);
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
            OpenAIChatResponse tmp;
            tmp.choices_message_content = "系统提示：无法将问题发送给OpenAI，请稍后再重试或联系管理员...";
            return tmp;
        }

        // 清理资源
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
#ifdef DEBUG
        std::cout << "OpenAI 原始消息：" << response << std::endl;
#endif

        // 响应格式化
        OpenAIChatResponse responseFormat = this->chat_json_parse(response);
        // 无误返回
        return {};
    }

    LOG_ERROR("无法创建http请求...");
    return {};
}

// 调用视觉模型
OpenAIVisionResponse OpenAIStandard::send_to_vision(const std::string &data, const std::string &base64, std::string model, std::string endpoint, std::string api_key)
{
    // api和端点纠正
    endpoint = OpenAIStandard::filterNonNormalChars(endpoint);
    api_key = OpenAIStandard::filterNonNormalChars(api_key);
    endpoint = OpenAIStandard::filterNonNormalChars(endpoint);
    api_key = OpenAIStandard::filterNonNormalChars(api_key);

    nlohmann::json content = nlohmann::json::array();
    content.push_back({{"type", "text"}, {"text", data}});
    content.push_back({{"type", "image_url"},
                       {"image_url", {{"url", "data:image/jpeg;base64," + base64}}}});

    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "user"}, {"content", content}});

    nlohmann::json payload = {
        {"model", model},
        {"messages", messages}};
    std::string json_string = payload.dump();

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

        std::string response;
        // 封装HTTP POST数据报 + 设置libcurl选项
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str()); // 添加端点
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_send_char_message.dump().c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_chat);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        OpenAIVisionResponse Data;
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
            Data.choice_message_content = "系统提示：无法将问题发送给OpenAI，请稍后再重试或联系管理员...";
            LOG_ERROR("无法请求...");
            return Data;
        }

        // 清理资源
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
#ifdef DEBUG
        std::cout << "OpenAI 返回的原始消息：" << data << std::endl;
#endif
        // 判断数据合格性
        // if (!isMessageComplete(data))
        // {
        //     return {};
        // }
        // 响应数据格式化
        OpenAIVisionResponse responseFormat = this->vision_json_parse(data);
        // 无误返回
        std::cout << "无误返回:" << data << std::endl;
        return responseFormat;
    }

    OpenAIVisionResponse tmp;
    tmp.choice_message_content = "系统提示：无法跟OpenAI连接，请联系管理员...";
    return tmp;
}

// 调用绘图模型
OpenAIImageResponse OpenAIStandard::send_to_draw(const std::string &prompt, std::string model, std::string endpoint, std::string api_key)
{
    // api和端点纠正
    endpoint = OpenAIStandard::filterNonNormalChars(endpoint);
    api_key = OpenAIStandard::filterNonNormalChars(api_key);

    CURL *curl;
    CURLcode res;

    // 初始化Curl
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

        // 设置请求体
        nlohmann::json postData;
        postData["model"] = model;
        postData["prompt"] = prompt;
        postData["n"] = 1;
        postData["size"] = "1024x1024";

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.dump().c_str());

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

        // 解析内容
        OpenAIImageResponse responseFormat = this->draw_json_parse(prompt);
        responseFormat.code = static_cast<int>(http_code);

        // 清理
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        return responseFormat;
    }
    return {};
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
        responseFormat.id = json_data.value("id", "");
        responseFormat.object = json_data.value("object", "");
        responseFormat.created = json_data.value("created", 0);
        responseFormat.model = json_data.value("model", "");
        if (json_data.contains("usage") && json_data["usage"].is_object())
        {
            auto &usage = json_data["usage"];
            responseFormat.usage_prompt_tokens = usage.value("prompt_tokens", 0);
            responseFormat.usage_completion_tokens = usage.value("completion_tokens", 0);
            responseFormat.usage_total_tokens = usage.value("total_tokens", 0);
        }

        if (json_data["choices"].is_array())
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
        nlohmann::json jsonData = nlohmann::json::parse(response);
        OpenAIImageResponse responseFormat;
        responseFormat.created = jsonData.value("created", 0);
        responseFormat.output_format = jsonData.value("output_format", "");
        responseFormat.quality = jsonData.value("quality", "");
        responseFormat.size = jsonData.value("size", "");
        if (jsonData.contains("data") && jsonData["data"].is_array() && !jsonData["data"].empty())
        {
            responseFormat.data_base64 = jsonData["data"][0].value("b64_json", "");
        }

        if (jsonData.contains("usage") && jsonData["usage"].is_object())
        {
            const auto &data = jsonData["usage"];
            responseFormat.usage_input_tokens = data.value("input_tokens", 0);
            responseFormat.usage_output_tokens = data.value("output_tokens", 0);
            responseFormat.usage_total_tokens = data.value("total_tokens", 0);
        }
        return responseFormat;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }
    return {};
}

OpenAIVisionResponse OpenAIStandard::vision_json_parse(const std::string &response)
{
    OpenAIVisionResponse Data;
    try
    {
        nlohmann::json doc = nlohmann::json::parse(response);

        // 获取 choice 中的内容
        if (doc["choice"].is_array() && !doc["choice"].empty() && doc["choice"][0].is_object())
        {
            nlohmann::json choice = doc["choice"][0];
            if (choice["message"].is_object())
            {
                Data.choice_message_content = choice["message"].value("content", "");
                Data.choice_message_refusal = choice["message"].value("refusal", "");
            }
        }

        // 获取 usage 中的内容
        if (doc["usage"].is_object())
        {
            nlohmann::json usage = doc["usage"];
            Data.usage_completion_tokens = usage.value("completion_tokens", 0);
            Data.usage_prompt_tokens = usage.value("prompt_tokens", 0);
            Data.usage_total_tokens = usage.value("total_tokens", 0);
        }

        Data.model = doc.value("model", "");
        Data.created = doc.value("created", 0);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("返回的内容不是有效的Json对象。详细：" + std::string(e.what()));
        return {};
    }
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

// 消息完整性判断
// bool OpenAIStandard::isMessageComplete(std::string &message)
// {
//     // 若出现以下问题，则消息不完整
//     if (isTimeOut(message) || isKeyError(message))
//     {
//         return false;
//     }
//     else if (message.size() < 100)
//     {
//         return false;
//     }

//     // ...这里设置其他错误判断

//     return true;
// }

// 超时判断
// bool OpenAIStandard::isTimeOut(std::string &message)
// {
//     // 所有来自OpenAI的错误代码都将注册在此处
//     std::vector<std::string> errorCode;
//     errorCode.push_back("<head><title>504 Gateway Time-out</title></head>");
//     errorCode.push_back("error code: 524");
//     // 此处push_back其他错误代码...

//     for (const auto str : errorCode)
//     {
//         if (message.find(str) != message.npos)
//         {
//             LOG_ERROR(message);
//             message = "系统提示：时间超时,请重新发送...";
// #ifdef DEBUG
//             std::cout << "时间超时..." << std::endl;
// #endif
//             return true;
//         }
//     }
//     return false;
// }

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

/*
std::string OpenAIStandard::ResponseJsonVerify(std::string str, std::string sub)
{
    LOG_DEBUG("裁剪前：" + str);
    // 该函数的主要目的是把json后面的东西给分割掉
    return str.substr(0, str.rfind(sub) + sub.size());
}
*/

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