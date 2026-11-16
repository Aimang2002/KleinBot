#ifndef VOICE_H
#define VOICE_H

#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include "VoiceOptions.h"
#include "../../Log/Log.h"
#include "../../utils/Live.h"

class Voice
{
public:
    // config 是组合根的活值（ADR-023）：每次合成开头取一份快照，改了参数下一次合成即用新值
    explicit Voice(const Live<VoiceOptions> &config, const std::atomic<bool> *running = nullptr);
    /**
     * @brief 文本转语音
     *
     * @param text 	要说的话
     *
     *@return   成功返回音频文件的绝对路径；任何失败（建不了文件、连不上、服务返回非 200、
     *          程序停止取消）都返回空字符串，并把服务标记为不可用，直到下一次探测
     */
    std::string toAudio(const std::string &text);
    /**
     * @brief 语音服务此刻是否可用（send_voice 是否对模型开放的依据之一）
     *
     * 结果缓存 kProbeIntervalSeconds 秒：到期或配置的地址变了才重新探测一次——
     * 只做一次 TCP 连接（1 秒超时），不合成。合成失败会立即把它置为不可用。
     */
    bool reachable();
    /**
     * @brief 获取base64编码
     *
     * @param input 	源数据
     *
     *@return   正常返回base64编码，错误则返回空字符串
     */
    std::string dataToBase64(const std::string &input);
    ~Voice();

private:
    static constexpr std::int64_t kProbeIntervalSeconds = 60;
    void markUnreachable(const std::string &target);
    static std::string targetOf(const VoiceOptions &options);

    const Live<VoiceOptions> &config;
    const std::atomic<bool> *running;
    std::mutex healthMutex;          // 只保护下面三项缓存，探测本身在锁外
    std::string probedTarget;        // 上次探测的 host:port
    std::int64_t probedAt = 0;       // 上次探测（或合成失败）的时刻，秒
    bool lastReachable = false;
};

#endif
