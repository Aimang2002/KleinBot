#include "../../ConfigManager/ConfigManager.h"
#include <iostream>
#include <vector>

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
     *@return  返回文件路径
     */
    std::string fixImageSizeTo4K(const std::string &message);
    std::vector<std::string> getFileSuffix(const std::string directoryPath);
    ~Realesrgan();

private:
};