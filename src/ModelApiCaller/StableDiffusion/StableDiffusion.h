#ifndef STABLEDIFFUSION_H
#define STABLEDIFFUSION_H

#include "../../JsonParse/JsonParse.h"
#include "../../ConfigManager/ConfigManager.h"
#include <fstream>
#include <string>

// extern ConfigManager &ConfigManager::getInstance();
// extern JsonParse &JParsingClass;

class StableDiffusion
{
public:
    // 连接StableDiffusion获取数据，steps为图像所需的步数，返回的是base64编码
    static std::string connectStableDiffusion(std::string prompt);

private:
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
};

#endif
