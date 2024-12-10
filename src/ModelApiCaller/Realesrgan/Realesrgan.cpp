#include "Realesrgan.h"

Realesrgan::Realesrgan()
{
}

std::string Realesrgan::dataToBase64(const std::string &input)
{
    const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string encoded;
    int val = 0;
    int bits = -6;
    const unsigned int mask = 0x3F; // 0b00111111

    for (unsigned char c : input)
    {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0)
        {
            encoded.push_back(base64_chars[(val >> bits) & mask]);
            bits -= 6;
        }
    }

    if (bits > -6)
    {
        encoded.push_back(base64_chars[((val << 8) >> (bits + 8)) & mask]);
    }

    while (encoded.size() % 4 != 0)
    {
        encoded.push_back('=');
    }

    return encoded;
}

size_t realesrgan_write_data(void *ptr, size_t size, size_t nmemb, std::string *data)
{
    data->append(reinterpret_cast<const char *>(ptr), size * nmemb);
    return size * nmemb;
}

short Realesrgan::fixImageSizeTo4K(std::string &message)
{
    std::string url = message.substr(message.find("url=") + 4, message.find("]") - (message.find("url=") + 4)); // 提取URL
    if (url.size() < 10)
    {
        message = "系统提示：似乎没有图片...";
        return -1;
    }

    // URL修正
    size_t index = 0;
    do
    {
        std::string findWord = "&amp;";
        index = url.find(findWord);
        if (index != std::string::npos)
        {
            url.erase(index, findWord.size());
            url.insert(index, "&");
        }
    } while (index != std::string::npos);

    std::string filename;
    // 随机名称
    for (int i = 0; i < 18; i++)
    {
        unsigned short number = rand() % 26 + 65;
        filename += (char)number;
    }

    std::string inputPath = CManager.configVariable("IMAGE_DOWNLOAD_PATH") + filename;
    std::string outputPath = inputPath;
    outputPath += "_4k";
    inputPath += ".jpg";
    outputPath += ".jpg";

    // 下载图片
    {
        std::string image_data; // 用于接收图片二进制数据

        // 初始化 curl
        CURL *curl = curl_easy_init();
        if (!curl)
        {
            // std::cerr << "Failed to initialize curl" << std::endl;
            LOG_ERROR("Failed to initialize curl");
            message = "系统提示：Failed to initialize curl";
            return -1;
        }

        // 下载
        // 设置SSL证书验证
#if defined(__WIN32) || defined(__WIN64)
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CAINFO, "cacert.pem");
#endif
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, realesrgan_write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &image_data);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            LOG_ERROR(std::string("Failed to download image: ") + curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            message = "系统提示：Failed to download image: " + std::string(curl_easy_strerror(res));
            return -1;
        }

        // 以二进制写入方式打开文件
        std::ofstream outfile(inputPath, std::ios::binary);
        if (!outfile.is_open())
        {
            LOG_ERROR(inputPath + "Failed to open for writing");
            message = "系统提示：Failed to open for writing.";
            return -1;
        }

        // 将数据写入文件中
        outfile.write(image_data.c_str(), image_data.size());
        outfile.close();

        // 清理 curl 实例
        curl_easy_cleanup(curl);
    }

    // 对图片进行4K修复
    {
        std::string command = "" + CManager.configVariable("REALESGAN_PATH") + "realesrgan-ncnn-vulkan -i ";
        command += inputPath + " -o ";
        command += outputPath + " -n " + CManager.configVariable("REALESGAN_MODEL");
        command += " > /dev/null 2>&1"; // 丢弃日志输出 Windows中是 >nul 2>&1

        LOG_INFO("图片修复中...");
        int returnCode = system(command.c_str());

        if (returnCode == 0)
        {
            LOG_INFO("修复完成！");
        }
        else
        {
            LOG_ERROR("修复失败！");
            message = "系统提示：修复失败！";
            return -1;
        }
    }

    // 发送已经修复的图片
    std::ifstream ifs(outputPath, std::ios::binary);
    // 如果能打开图片，就使用base64编码发送
    if (ifs.is_open())
    {
        // 一次性读取文件到content中
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        // 判断文件大小是否超过30M
        if ((content.size() / 1024 / 1024) > 30)
        {
            LOG_WARNING("图片大小超过30MB，不予发送！");
            message = "系统提示：修复后的图片大于30MB，无法发送！";
            return -30;
        }
        content = this->dataToBase64(content);
        ifs.close();

        // 清理文件
        if (remove(outputPath.c_str()) != 0)
        {
            LOG_WARNING("文件“" + outputPath + "”清理失败!");
        }
        if (remove(inputPath.c_str()) != 0)
        {
            LOG_WARNING("文件“" + inputPath + "”清理失败!");
        }
        message = content;
        return 2;
    }

    LOG_WARNING("文件打开失败，使用file方式发送图片(这种方式不能清除原图和4K图片)...");
    // outputPath.insert(0, "file://"); // =base64://
    // message = CQCode("image", "file", outputPath);
    message = outputPath;
    return 1;
}

std::vector<std::string> Realesrgan::getFileSuffix(const std::string directoryPath)
{
    std::vector<std::string> files;
    for (const auto &entry : std::filesystem::directory_iterator(directoryPath))
    {
        files.push_back(entry.path().filename().string());
    }
    return files;
}

Realesrgan::~Realesrgan()
{
}