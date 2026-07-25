#include "StableDiffusion.h"
#include "../Log/Log.h"
#include <curl/curl.h>
#include <iostream>

std::string StableDiffusion::connectStableDiffusion(const std::string prompt)
{
    // prompt = R"("lou tianyi")";
    // v2.2.4版本正向提示词硬编码写入，v2.3.0版本修改为软编码
    std::string revised_prompt = "(((best quality))),(((ultra detailed))),(((masterpiece))),illustration,";
    std::string payload = prompt; // JsonParse::getInstance().toJson(prompt);

    // 构造请求的内容
    nlohmann::json HTTPPkage;
    HTTPPkage = {
        {"prompt", revised_prompt + payload},
        {"steps", 35}};


    // 初始化curl
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl)
    {
        // 配置API的URL
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());

        // 配置HTTP POST
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, HTTPPkage.dump().c_str());

        // 设置回调函数以便抓取服务器的响应
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // 设置Content-Type头信息
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // 发送请求并捕获响应
        res = curl_easy_perform(curl);

        if (res != CURLE_OK)
        {
            LOG_ERROR(std::string("curl_easy_perform() failed: ") + curl_easy_strerror(res));
            return {};
        }
        else
        {
            std::string base64_data;
            try
            {
                nlohmann::json doc = nlohmann::json::parse(readBuffer);
                if (!doc.is_object() || doc.contains("images") == false || doc["images"].is_array() == false || doc["images"].size() == 0)
                {
                    LOG_ERROR("Invalid JSON response");
                    return {};
                }
                base64_data = doc["images"][0].get<std::string>();
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("无法有效解释Json，原内容：" + readBuffer + ",错误信息：" + std::string(e.what()));
                return {};
            }
            return base64_data;
        }

        // 清理
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return std::string("系统提示：无法连接StableDiffusion!");
}

// 回调函数
size_t StableDiffusion::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}