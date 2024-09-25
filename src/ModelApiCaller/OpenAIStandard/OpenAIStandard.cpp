#include "OpenAIStandard.h"

// 文本翻译
bool OpenAIStandard::text_translate(std::string &text, const std::string model, std::string language, string endpoint, string api_key)
{
    LOG_INFO("使用了文本翻译");
    // 调整格式
    string ss;
    if (language == "EN")
    {
        ss = R"({"model":")" + model + "\",\"messages\":[";
        ss += R"({"role": "system", "content": "You will be provided with a sentence in English, and your task is to translate it into English."},
        {"role": "assistant", "content": "OK"},
        {"role": "user", "content": ")" +
              text + "\"}]";
        ss += R"(,"temperature":0.1,"top_p":0.9,"frequency_penalty":0,"presence_penalty":0)" + string("}"); // 超参数
    }
    if (language == "ZH")
    {
        ss = R"("model":")" + model + "\",\"messages\":[";
        ss += R"([
        {"role": "system", "content": "翻译成中文"},
        {"role": "assistant", "content": "好的"},
        {"role": "user", "content": ")";
        ss += text + "\"}]";
        ss += R"(,"temperature":0.1,"top_p":0.9,"frequency_penalty":0,"presence_penalty":0)" + string("}");
    }

    text = ss;

    send_to_chat(text, model, endpoint, api_key);

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

bool OpenAIStandard::send_to_chat(string &data, string models, string endpoint, string api_key)
{
    /*
     * data参数里必须构建好Json数据以及prompt，本函数不提供Json数据封装
     */

#ifdef DEBUG
    LOG_INFO("使用模型:" + models);
#endif

    // api和端点纠正
    endpoint = OpenAIStandard::filterNonNormalChars(endpoint);
    api_key = OpenAIStandard::filterNonNormalChars(api_key);

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

        string json_data = data;
        // LOG_DEBUG("发送内容：" + json_data);
        data = "";
        // 封装HTTP POST数据报 + 设置libcurl选项
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str()); // 添加端点
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_chat);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);

        // 执行HTTP请求
        unsigned short request = 5;
        while (request--)
        {
            res = curl_easy_perform(curl);
            if (res != CURLE_OK)
            {
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                sleep(1);
                continue;
            }
            break;
        }
        if (request < 1)
        {
            data = "系统提示：无法将问题发送给OpenAI，请稍后再重试或联系管理员...";
            return false;
        }

        // 清理资源
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

#ifdef DEBUG
        cout << "OpenAI 原始消息：" << data << endl;
#endif
        // 判断数据合格性
        if (!isMessageComplete(data))
        {
            return false;
        }
        // 无误返回
        return true;
    }

    data = "系统提示：无法跟OpenAI连接，请联系管理员...";
    return false;
}

// 调用视觉模型
bool OpenAIStandard::send_to_vision(std::string &data, std::string &base64, string model, string endpoint, string api_key)
{
    // api和端点纠正
    endpoint = OpenAIStandard::filterNonNormalChars(endpoint);
    api_key = OpenAIStandard::filterNonNormalChars(api_key);

    // 封装json格式
    std::string json_string = "[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"";
    json_string += data;
    json_string += "\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
    json_string += base64;
    json_string += "\"}}]}]";

    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();

    if (curl)
    {
        // 设置API密钥
        struct curl_slist *headers = NULL;
        std::string header_auth = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, header_auth.c_str());

        // 设置请求数据，包括上下文
        std::string payload_send_char_message = "{\"model\":\"" + model + "\",\"messages\":" + json_string + "}";
        data = ""; // 置为空
        // 封装HTTP POST数据报 + 设置libcurl选项
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str()); // 添加端点
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_send_char_message.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_chat);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);

        // 执行HTTP请求
        unsigned short request = 5;
        while (request--)
        {
            res = curl_easy_perform(curl);
            if (res != CURLE_OK)
            {
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                sleep(1);
                continue;
            }
            break;
        }
        if (request < 1)
        {
            data = "系统提示：无法将问题发送给OpenAI，请稍后再重试或联系管理员...";
            return false;
        }

        // 清理资源
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

#ifdef DEBUG
        cout << "OpenAI 返回的原始消息：" << data << endl;
#endif
        // 判断数据合格性
        if (!isMessageComplete(data))
        {
            return false;
        }
        // 无误返回
        cout << "无误返回:" << data << endl;
        return true;
    }

    data = "系统提示：无法跟OpenAI连接，请联系管理员...";
    return false;
}

// 调用绘图模型
bool OpenAIStandard::send_to_draw(std::string &prompt, string model, string endpoint, string api_key)
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
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string authorization = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, authorization.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // 设置请求体
        std::string postData = "{\"model\": \"" + model + "\", \"prompt\": \"" + prompt + "\", \"n\": 1, \"size\": \"1024x1024\"}";
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());

        // 设置响应回调函数
        prompt = " ";
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_chat);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &prompt);

        // 执行HTTP请求
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            LOG_ERROR("curl_easy_strerror(res)");
        }
        else
        {
            cout << "OpenAI 返回的原始消息：" << prompt << endl;
        }

        // 清理
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        return true;
    }
    return false;
}

// 回调函数
size_t OpenAIStandard::write_callback_chat(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t response_size = size * nmemb;
    // 获取类实例指针
    std::string *instance = static_cast<std::string *>(userdata);
    // 保存接收到的内容
    *instance += std::string(ptr);
    return response_size;
}

// 消息完整性判断
bool OpenAIStandard::isMessageComplete(string &message)
{
    // 若出现以下问题，则消息不完整
    if (isTimeOut(message))
        return false;
    else if (isKeyError(message))
        return false;
    // ...这里设置其他错误判断

    return true;
}

// 超时判断
bool OpenAIStandard::isTimeOut(string &message)
{
    // 所有来自OpenAI的错误代码都将注册在此处
    std::vector<std::string> errorCode;
    errorCode.push_back("<head><title>504 Gateway Time-out</title></head>");
    errorCode.push_back("error code: 524");
    // 此处push_back其他错误代码...

    for (const auto str : errorCode)
    {
        if (message.find(str) != message.npos)
        {
            message = "系统提示：时间超时,请重新发送...";
#ifdef DEBUG
            cout << "时间超时..." << endl;
#endif
            return true;
        }
    }
    return false;
}

// Key 错误判断
bool OpenAIStandard::isKeyError(string &message)
{
    if (message.find("无效的令牌") != message.npos)
    {
        message = "系统提示：Key 有误！";
#ifdef DEBUG
        LOG_ERROR("Key有误！");
#endif
        return true;
    }
    else if (message.find("该令牌额度已用尽") != message.npos)
    {
        message = "系统提示：Key额度用完！请联系管理员...";
#ifdef DEBUG
        LOG_ERROR("Key额度已用完！");
#endif
        return true;
    }
    return false;
}

// API和端点修正
string OpenAIStandard::filterNonNormalChars(string str)
{
    string result;
    for (char c : str)
    {
        if (std::isprint(c) && !std::isspace(c))
        {
            result += c;
        }
    }
    return result;
}