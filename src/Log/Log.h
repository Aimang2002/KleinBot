#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <string>
/*
namespace LOGNAMESPACE
{
    bool isUtf8Start(char byte)
    {
        return (byte & 0xC0) != 0x80;
    }

    std::string utf8substr(const std::string &str, size_t start, size_t length)
    {
        size_t byteStart = 0;
        size_t charCount = 0;

        // 找到开始位置
        for (size_t i = 0; i < str.size(); ++i)
        {
            if (charCount == start)
            {
                byteStart = i;
                break;
            }
            if (isUtf8Start(str[i]))
            {
                ++charCount;
            }
        }

        // 找到结束位置
        size_t byteEnd = byteStart;
        for (size_t i = byteStart; i < str.size(); ++i)
        {
            if (charCount == start + length)
            {
                byteEnd = i;
                break;
            }
            if (isUtf8Start(str[i]))
            {
                ++charCount;
            }
        }

        return str.substr(byteStart, byteEnd - byteStart);
    }
}

*/

#define LOG_DEBUG(message)        \
    std::cout << "\033[38;5;208m" \
              << "DEBUG: " << message << "\033[0m" << std::endl

#define LOG_INFO(message)   \
    std::cout << "\033[32m" \
              << "INFO: " << message << "\033[0m" << std::endl

#define LOG_WARNING(message) \
    std::cout << "\033[33m"  \
              << "WARNING: " << message << "\033[0m" << std::endl

#define LOG_ERROR(message)  \
    std::cerr << "\033[31m" \
              << "ERROR: " << message << "\033[0m" << std::endl

#define LOG_FATAL(message)    \
    std::cerr << "\033[1;31m" \
              << "FATAL: " << message << "\033[0m" << std::endl

#endif