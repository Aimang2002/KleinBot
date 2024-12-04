#include "Voice.h"

Voice::Voice() {}

// 回调函数，用于处理HTTP响应数据
size_t so_VIST_WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    std::ostream *os = static_cast<std::ostream *>(userp);
    size_t totalSize = size * nmemb;
    os->write(static_cast<const char *>(contents), totalSize);
    return totalSize;
}

int Voice::toAudio(std::string &text)
{
    LOG_INFO("使用了文本转语音");
    /*
     * 目前文本转语音仅支持中文
     */

    // 检查文本是否合格
    if (text.size() < 4)
    {
        text = "系统提示：未检查到文本...";
        return -1;
    }

    // 创建语音文件
    auto now = std::chrono::system_clock::now();
    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    std::string filename = std::to_string(timestamp); // 获取时间戳作为文件名
    filename += ".wav";
    std::string filePath = CManager.configVariable("VITS_FILE_SAVE_PATH") + filename;

    // 创建文件用来保存音频数据
    std::ofstream audioFile(filePath, std::ios::binary);
    if (!audioFile.is_open())
    {
        std::cerr << "无法创建输出文件" << std::endl;
        text = "系统提示：内部错误！";
        return -1;
    }

    CURL *curl;
    CURLcode res;

    // 指定必要内容
    std::string url = CManager.configVariable("VITS_API_URL") + ":" + CManager.configVariable("VITS_API_PORT") + "/tts"; // "127.0.0.1:9880/tts";
    std::string postData = "{\n";
    postData += "\t\"text\":\"" + text + "\",\n";
    postData += "\t\"text_lang\":\"zh\",\n";
    postData += "\t\"ref_audio_path\":\"" + CManager.configVariable("VITS_REFERVOICE_PATH") + "\",\n";
    postData += "\t\"prompt_text\":\"" + CManager.configVariable("VITS_REFERVOICE_TEXT") + "\",\n";
    postData += "\t\"prompt_lang\":\"zh\",\n";
    postData += "\t\"streaming_mode\":false\n"; // 流模式默认关闭（用于实现实时推理跟播放）
    postData += "}";

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
            std::cerr << "请求失败: " << curl_easy_strerror(res) << std::endl;
            text = "系统提示：请求失败:" + std::string(curl_easy_strerror(res));
            // 释放
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            return -1;
        }
        else
        {
            audioFile.close();
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            text = filePath;
            return 1;
        }
    }
    return -1;
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