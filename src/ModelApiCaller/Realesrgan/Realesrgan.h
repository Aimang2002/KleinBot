#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <filesystem>
#include <vector>
#include "../../Log/Log.h"
#include "../../ConfigManager/ConfigManager.h"

extern ConfigManager &CManager;

class Realesrgan
{
public:
    Realesrgan();
    std::string dataToBase64(const std::string &input);

    /**
     * @brief 修复图片
     *
     * @param message 	源数据
     *
     *@return   返回-30，文件超过限定发送的最大值；
                返回-1.处理有误；
                返回1，使用路径传输;
                返回2，使用base64编码传输;
     */
    short fixImageSizeTo4K(std::string &message);
    vector<std::string> getFileSuffix(const std::string directoryPath);
    ~Realesrgan();

private:
};