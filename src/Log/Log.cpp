#include "Log.h"

int Log::maxLength = 300;
std::mutex Log::__mutex = std::mutex();
std::string Log::logName = "defualt.log";

void Log::info(std::string message)
{
    std::cout << "\033[32m"
              << "[" << getNowTime("y+h") << "] " << "INFO: " << messageLengthCheck(message) << "\033[0m" << std::endl;
}

void Log::debug(std::string message)
{
    std::cout << "\033[38;5;208m"
              << "[" << getNowTime("y+h") << "] " << "DEBUG: " << messageLengthCheck(message) << "\033[0m" << std::endl;
}

void Log::warning(std::string message)
{
    std::string result = messageLengthCheck(message);
    record(message);
    std::cout << "\033[33m"
              << "[" << getNowTime("y+h") << "] " << "WARNING: " << message << "\033[0m" << std::endl;
}

void Log::error(std::string message)
{
    std::string result = messageLengthCheck(message);
    record(message);
    std::cerr << "\033[1;31m"
              << "[" << getNowTime("y+h") << "] " << "ERROR: " << message << "\033[0m" << std::endl;
}

void Log::fatal(std::string message)
{
    std::string result = messageLengthCheck(message);
    record(message);
    std::cerr << "\033[31m"
              << "[" << getNowTime("y+h") << "] " << "FATAL: " << message << "\033[0m" << std::endl;
}

std::string Log::messageLengthCheck(std::string message)
{
    if (message.size() <= maxLength)
    {
        return message;
    }

    const char *chs = message.c_str();
    int end = 0;
    while (end < strlen(chs) && end < maxLength)
    {
        end += ((unsigned int)chs[end] > 0x80) ? 3 : 1;
    }
    if (end > message.length())
    {
        end = message.length();
    }
    return message.substr(0, end) + "...";
}

std::string Log::getNowTime(std::string ch, std::string split)
{
    time_t now = time(nullptr);
    tm *t = localtime(&now);
    std::stringstream sstring;

    if (ch == "y")
    {
        sstring << (1900 + t->tm_year) << split << t->tm_mon << split << t->tm_mday;
    }
    else if (ch == "y+h")
    {
        sstring << (1900 + t->tm_year) << split << t->tm_mon << split << t->tm_mday << " "
                << t->tm_hour << split << t->tm_min << split << t->tm_sec;
    }
    else if (ch == "h")
    {
        sstring << t->tm_hour << split << t->tm_min << split << t->tm_sec;
    }
    return sstring.str();
}

void Log::record(const std::string message)
{
    // 性能损耗太大，有时间记得优化...

    std::unique_lock<std::mutex> locker(__mutex);
    std::ofstream ofs(logName, std::ios::app);
    if (!ofs.is_open())
    {
        std::cout << "日志信息无法写入！" << std::endl;
        return;
    }
    ofs << "[" << getNowTime("y+h") << "] " << message << std::endl;
    ofs.close();
}