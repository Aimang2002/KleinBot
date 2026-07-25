#ifndef STABLEDIFFUSION_H
#define STABLEDIFFUSION_H

#include "../../JsonParse/JsonParse.h"
#include <fstream>
#include <string>

class StableDiffusion
{
public:
    explicit StableDiffusion(std::string endpoint) : endpoint(std::move(endpoint)) {}
    // 连接StableDiffusion获取数据，steps为图像所需的步数，返回的是base64编码
    std::string connectStableDiffusion(const std::string prompt);

private:
    std::string endpoint;
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
};

#endif
