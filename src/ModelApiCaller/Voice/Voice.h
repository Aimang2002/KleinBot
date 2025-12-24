#ifndef VOICE_H
#define VOICE_H

#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <chrono>
#include "../../ConfigManager/ConfigManager.h"
#include "../../Log/Log.h"

// extern ConfigManager &ConfigManager::getInstance();

class Voice
{
public:
    Voice();
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
};

#endif