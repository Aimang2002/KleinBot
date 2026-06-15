#ifndef RWKV_H
#define RWKV_H

// 临时状态，后期需要更改，不然会打乱整体框架
#include <iostream>
#include <unistd.h>
#include <curl/curl.h>
/*
class RWKV
{
public:
    static bool GPT(std::string &data)
    {
        data.erase(data.find("["), 1);
        data.erase(data.rfind("]"), 1);
        LOG_DEBUG(data);

        CURL *curl;
        CURLcode res;
        std::string readBuffer;

        string api_key = CManager.configVariable("RWKV_MODEL_API_KEY");
        string endpoint = CManager.configVariable("RWKV_MODEL_ENDPOINT") + "/v1/chat/completions";

        LOG_DEBUG(api_key);
        LOG_DEBUG(endpoint);

        // 初始化curl
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl = curl_easy_init();
        if (curl)
        {
            // 设置URL
            curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str()); // http://192.168.191.194:11451/v1/chat/completions

            // 设置HTTP头，包括API密钥
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, api_key.c_str()); // sk-jPKn0hkmqlKyecK1Fe9e8f4aB1Ad48D8874a42A422Ef0
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            // 设置POST数据

            std::string jsonData = R"({
            "frequency_penalty": 1,
            "max_tokens": 1000,
            "messages": [)";
            jsonData += data;
            jsonData += R"(],
            "model": "rwkv",
            "temperature": 2,
            "top_p": 0.55,
            "frequency Penalty":0.4,
            "presence_penalty": 0.4
        })";
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());

            // 设置回调函数以处理响应数据
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

            // 执行请求
            res = curl_easy_perform(curl);
            if (res != CURLE_OK)
            {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            }
            else
            {
                // 输出响应数据
                std::cout << "Response data: " << readBuffer << std::endl;
                data = readBuffer;
            }

            // 清理
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            return true;
        }

        curl_global_cleanup();
        return true;
    }

private:
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
    {
        ((std::string *)userp)->append((char *)contents, size * nmemb);
        return size * nmemb;
    }

private:
};
*/
#endif
