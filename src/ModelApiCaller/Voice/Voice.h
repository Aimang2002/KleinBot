#ifndef VOICE_H
#define VOICE_H

#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <chrono>
#include "../../ConfigManager/ConfigManager.h"
#include "../../Log/Log.h"

extern ConfigManager &CManager;

class Voice
{
public:
    Voice();
    /**
     * @brief 修复图片
     *
     * @param message 	源数据
     *
     *@return   返回-1.处理有误；
                返回1，使用路径传输;
                返回2，使用base64编码传输;
     */
    int toAudio(std::string &text);
    std::string dataToBase64(const std::string &input);
    ~Voice();

private:
};

#endif