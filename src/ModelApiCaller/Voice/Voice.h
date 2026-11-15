#ifndef VOICE_H
#define VOICE_H

#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <chrono>
#include <atomic>
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
     * @param message 	源数据
     *
     *@return   正常返回音频文件的绝对路径，错误则返回空字符串
     */
    std::string toAudio(const std::string &text);
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
    const Live<VoiceOptions> &config;
    const std::atomic<bool> *running;
};

#endif
