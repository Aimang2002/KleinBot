#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <cstring>
#include <sstream>
#include <fstream>
#include <queue>
#include <mutex>

class Log
{
public:
    static void info(std::string message);
    static void debug(std::string message);
    static void warning(std::string message);
    static void error(std::string message);
    static void fatal(std::string message);
    static std::string getNowTime(std::string ch = "h", std::string split = ":");

public:
    static std::string logName;

private:
    static std::string messageLengthCheck(std::string message);
    static void record(const std::string message);

private:
    static int maxLength;
    static std::mutex __mutex; // 静态锁，对日志写入进行控制
};

#define LOG_DEBUG(message) Log::debug(message)

#define LOG_INFO(message) Log::info(message)

#define LOG_WARNING(message) Log::warning(message)

#define LOG_ERROR(message) Log::error(message)

#define LOG_FATAL(message) Log::fatal(message)

#endif // LOG_H