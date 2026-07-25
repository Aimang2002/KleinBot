#ifndef VOICE_H
#define VOICE_H

#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <chrono>
#include "../../Configuration/AppConfig.h"
#include "../../Log/Log.h"

class Voice
{
public:
    explicit Voice(VoiceConfig config);
    /**
     * @brief 修复图片
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
    VoiceConfig config;
};

#endif