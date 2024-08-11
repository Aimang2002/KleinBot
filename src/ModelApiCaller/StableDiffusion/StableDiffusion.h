#ifndef STABLEDIFFUSION_H
#define STABLEDIFFUSION_H

#include <iostream>
#include <string>
#include <curl/curl.h>
#include <fstream>
#include "../../JsonParse/JsonParse.h"
#include "../../ConfigManager/ConfigManager.h"

extern ConfigManager &CManager;
extern JsonParse &JParsingClass;

class StableDiffusion
{
public:
    // 连接StableDiffusion获取数据，steps为图像所需的步数，返回的是base64编码
    static std::string connectStableDiffusion(std::string prompt);

private:
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
};

#endif
