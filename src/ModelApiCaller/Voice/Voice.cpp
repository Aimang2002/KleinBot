#include "Voice.h"
#include "../../Library/nlohmann/json.hpp"
#include "../../Network/CurlRequestControl.h"
#include <ctime>
#include <filesystem>

Voice::Voice(const Live<VoiceOptions> &config, const std::atomic<bool> *running)
    : config(config), running(running) {}

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
    // 一次合成取一次配置快照，之后都用这一份
    const Live<VoiceOptions>::Snapshot options = config.get();

    // 语音文件写入系统临时目录（Linux /tmp、Windows %TEMP%）下的专用子目录，
    // 发送成功后由 transport 层删除；文件名带毫秒时间戳 + 进程内序号，
    // 避免多个 worker 线程同一秒合成时互相覆盖
    std::error_code pathError;
    const std::filesystem::path voiceDir =
        std::filesystem::temp_directory_path(pathError) / "kleinbot";
    if (pathError)
    {
        LOG_ERROR("无法定位系统临时目录：" + pathError.message());
        return {};
    }
    std::error_code dirError;
    std::filesystem::create_directories(voiceDir, dirError);
    if (dirError)
    {
        LOG_ERROR("无法创建语音输出目录：" + voiceDir.string() + "，" + dirError.message());
        return {};
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
        return {};
    }
    const auto discardFile = [&audioFile, &filePath]() {
        audioFile.close();
        std::error_code removeError;
        std::filesystem::remove(filePath, removeError);
    };

    CURL *curl;
    CURLcode res;

    // 指定必要内容
    std::string url = options->host + ":" + options->port + "/tts"; // "127.0.0.1:9880/tts";
    nlohmann::json doc = {
        {"text", text},
        {"text_lang", "zh"},
        {"ref_audio_path", options->referenceAudioPath},
        {"prompt_text", options->referenceText},
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
            LOG_ERROR("语音合成请求失败：" + std::string(curl_easy_strerror(res)));
            discardFile();
            markUnreachable(targetOf(*options));
            return {};
        }
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        if (status != 200)
        {
            // 服务回了错误（如参考音频路径不对）：响应体不是音频，不能当语音发出去
            LOG_ERROR("语音合成服务返回 HTTP " + std::to_string(status));
            discardFile();
            markUnreachable(targetOf(*options));
            return {};
        }
        audioFile.close();
        return filePath.string();
    }
    discardFile();
    return {};
}

std::string Voice::targetOf(const VoiceOptions &options)
{
    return options.host + ":" + options.port;
}

void Voice::markUnreachable(const std::string &target)
{
    std::lock_guard<std::mutex> lock(healthMutex);
    probedTarget = target;
    probedAt = static_cast<std::int64_t>(std::time(nullptr));
    lastReachable = false;
}

bool Voice::reachable()
{
    const Live<VoiceOptions>::Snapshot options = config.get();
    const std::string target = targetOf(*options);
    if (options->host.empty() || options->port.empty())
        return false;
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    {
        std::lock_guard<std::mutex> lock(healthMutex);
        if (target == probedTarget && now - probedAt < kProbeIntervalSeconds)
            return lastReachable;
    }
    // 只建一次 TCP 连接，不发请求、不合成
    bool ok = false;
    if (CURL *curl = curl_easy_init())
    {
        const std::string url = target + "/";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 1500L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        ok = curl_easy_perform(curl) == CURLE_OK;
        curl_easy_cleanup(curl);
    }
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(healthMutex);
        changed = probedAt == 0 || lastReachable != ok || probedTarget != target;
        probedTarget = target;
        probedAt = now;
        lastReachable = ok;
    }
    if (changed)
        LOG_INFO(ok ? "语音合成服务可用：" + target : "语音合成服务不可达：" + target);
    return ok;
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
