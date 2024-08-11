#include "StableDiffusion.h"

std::string StableDiffusion::connectStableDiffusion(std::string prompt)
{
    // prompt = R"("lou tianyi")";
    // v2.2.4版本正向提示词硬编码写入，v2.3.0版本修改为软编码
    string revised_prompt = "(((best quality))),(((ultra detailed))),(((masterpiece))),illustration,";

    prompt = JParsingClass.toJson(prompt);

    // 构造请求的内容
    string HTTPPkage;
    HTTPPkage = R"({"prompt": ")";
    HTTPPkage += revised_prompt + prompt; // 填入修订词+提示
    HTTPPkage += R"(", "steps": 35})";    // 生成图像的步数

    // 获取端点
    std::string endpint = CManager.configVariable("STABLEDIFFUSION_ENDPOINT");

    // 初始化curl
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl)
    {
        // 配置API的URL
        curl_easy_setopt(curl, CURLOPT_URL, endpint.c_str());

        // 配置HTTP POST
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, HTTPPkage.c_str());

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
            return std::string("系统提示：无法连接至StableDiffusion，请联系管理员...");
        }
        else
        {
            // 硬解析，不采用rapidjson，对未来拓展有阻碍
            int begin = readBuffer.find("[") + 2;
            std::string base64_data = readBuffer.substr(begin, readBuffer.find("]") - begin - 1);
            return base64_data;

            /* 使用RapidJSON，但解析失败
            std::string key = "images"; // 所需查找的key
            std::string value;

            bool result = JParsingClass.findKeyAndValue(readBuffer.c_str(), key, value);
            if (result)
            {
                return value;
            }
            else
            {
                LOG_ERROR("First image data not found in the images list");
                return std::string("系统提示：StableDiffusion返回的内容");
            }
            */
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