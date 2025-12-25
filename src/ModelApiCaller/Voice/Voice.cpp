#include "Voice.h"
#include "../../Library/nlohmann/json.hpp"
#include <filesystem>

Voice::Voice() {}

// 回调函数，用于处理HTTP响应数据
size_t so_VIST_WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    std::ostream *os = static_cast<std::ostream *>(userp);
    size_t totalSize = size * nmemb;
    os->write(static_cast<const char *>(contents), totalSize);
    return totalSize;
}

std::string Voice::toAudio(const std::string &text)
{
    LOG_INFO("使用了文本转语音");
    /*
     * 目前文本转语音仅支持中文
     */

    // 创建语音文件
    auto now = std::chrono::system_clock::now();
    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    std::filesystem::path filePath = ConfigManager::getInstance().configVariable("VITS_FILE_SAVE_PATH");
    filePath /= std::to_string(timestamp) + ".wav";

    LOG_DEBUG("语音文件：" + filePath.string());

    // 创建文件用来保存音频数据
    std::ofstream audioFile(filePath, std::ios::binary);
    if (!audioFile.is_open())
    {
        LOG_ERROR("无法创建输出文件");
        return "系统提示：无法创建输出文件";
    }

    CURL *curl;
    CURLcode res;

    // 指定必要内容
    std::string url = ConfigManager::getInstance().configVariable("VITS_API_URL") + ":" + ConfigManager::getInstance().configVariable("VITS_API_PORT") + "/tts"; // "127.0.0.1:9880/tts";
    nlohmann::json doc = {
        {"text", text},
        {"text_lang", "zh"},
        {"ref_audio_path", ConfigManager::getInstance().configVariable("VITS_REFERVOICE_PATH")},
        {"prompt_text", ConfigManager::getInstance().configVariable("VITS_REFERVOICE_TEXT")},
        {"prompt_lang", "zh"},
        {"streaming_mode", false}};
    std::string postData = doc.dump();

    // 初始化CURL
    curl = curl_easy_init();
    if (curl)
    {
        // 设置URL
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());

        // 设置接收响应数据的回调函数
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, so_VIST_WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &audioFile);

        // 执行HTTP请求
        res = curl_easy_perform(curl);

        // 检查执行结果
        if (res != CURLE_OK)
        {
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            LOG_ERROR("请求失败: " + std::string(curl_easy_strerror(res)));
            return "系统提示：请求失败。";
        }
        else
        {
            audioFile.close();
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            return filePath;
        }
    }
    return {};
}

std::string Voice::dataToBase64(const std::string &input)
{
    const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string encoded;
    int val = 0;
    int bits = -6;
    const unsigned int mask = 0x3F; // 0b00111111

    for (unsigned char c : input)
    {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0)
        {
            encoded.push_back(base64_chars[(val >> bits) & mask]);
            bits -= 6;
        }
    }

    if (bits > -6)
    {
        encoded.push_back(base64_chars[((val << 8) >> (bits + 8)) & mask]);
    }

    while (encoded.size() % 4 != 0)
    {
        encoded.push_back('=');
    }

    return encoded;
}

Voice::~Voice() {}