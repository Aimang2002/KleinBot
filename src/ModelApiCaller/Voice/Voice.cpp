#include "Voice.h"
#include "../../Library/nlohmann/json.hpp"
#include "../../Network/CurlRequestControl.h"
#include <filesystem>

Voice::Voice(VoiceOptions config, const std::atomic<bool> *running)
    : config(std::move(config)), running(running) {}

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

    // 语音文件写入系统临时目录（Linux /tmp、Windows %TEMP%）下的专用子目录，
    // 发送成功后由 transport 层删除；文件名带毫秒时间戳 + 进程内序号，
    // 避免多个 worker 线程同一秒合成时互相覆盖
    std::error_code pathError;
    const std::filesystem::path voiceDir =
        std::filesystem::temp_directory_path(pathError) / "kleinbot";
    if (pathError)
    {
        LOG_ERROR("无法定位系统临时目录：" + pathError.message());
        return "系统提示：无法创建输出文件";
    }
    std::error_code dirError;
    std::filesystem::create_directories(voiceDir, dirError);
    if (dirError)
    {
        LOG_ERROR("无法创建语音输出目录：" + voiceDir.string() + "，" + dirError.message());
        return "系统提示：无法创建输出文件";
    }

    static std::atomic<uint64_t> fileSequence{0};
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    std::filesystem::path filePath = voiceDir /
        (std::to_string(nowMs.count()) + "_" +
         std::to_string(fileSequence.fetch_add(1)) + ".wav");

    LOG_DEBUG("语音文件：" + filePath.string());

    // 创建文件用来保存音频数据；请求失败时删除半成品，不留空文件
    std::ofstream audioFile(filePath, std::ios::binary);
    if (!audioFile.is_open())
    {
        LOG_ERROR("无法创建输出文件");
        return "系统提示：无法创建输出文件";
    }
    const auto discardFile = [&audioFile, &filePath]() {
        audioFile.close();
        std::error_code removeError;
        std::filesystem::remove(filePath, removeError);
    };

    CURL *curl;
    CURLcode res;

    // 指定必要内容
    std::string url = config.host + ":" + config.port + "/tts"; // "127.0.0.1:9880/tts";
    nlohmann::json doc = {
        {"text", text},
        {"text_lang", "zh"},
        {"ref_audio_path", config.referenceAudioPath},
        {"prompt_text", config.referenceText},
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
        CurlRequestControl::configure(curl, running);

        // 执行HTTP请求
        res = curl_easy_perform(curl);

        // 检查执行结果
        if (res != CURLE_OK)
        {
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            if (CurlRequestControl::wasCancelled(res, running))
            {
                LOG_INFO("语音请求已因程序停止而取消");
                discardFile();
                return {};
            }
            LOG_ERROR("请求失败: " + std::string(curl_easy_strerror(res)));
            discardFile();
            return "系统提示：请求失败。";
        }
        else
        {
            audioFile.close();
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            return filePath.string();
        }
    }
    discardFile();
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
