#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <thread>
#include <fstream>

class Log
{
public:
    static Log &getInstance();
    void info(std::string message);
    void debug(std::string message);
    void warning(std::string message);
    void error(std::string message);
    void fatal(std::string message);

private:
    Log();
    Log(const Log &) = delete;
    Log &operator=(const Log &) = delete;
    bool initThread();

    bool shutdownThread(); // 废弃

    /**
     * @brief 获取格式化的当前时间
     * @param ch 模式："y"(年月日), "h"(时分秒), "y+h"(完整日期时间)
     * @param split 分隔符 (如 "-" 或 ":")
     * @return 格式化后的时间字符串
     */
    std::string getCurrentTime(std::string ch = "h", std::string split = ":");

private:
    std::string messageLengthCheck(std::string message);
    void addMessage(const std::string message);
    void record(const std::string message);

private:
    int maxLength;
    bool isRunning;                       // 线程运行状态
    std::ofstream logFile;                // 日志文件对象
    std::queue<std::string> messageQueue; // 消息队列
    std::mutex mutex_;                    // 消息队列静态锁
    std::thread logThread;                // 负责日志写入的线程实例
    std::condition_variable cv;           // 条件变量
};

#define LOG_DEBUG(message) Log::getInstance().debug(message)

#define LOG_INFO(message) Log::getInstance().info(message)

#define LOG_WARNING(message) Log::getInstance().warning(message)

#define LOG_ERROR(message) Log::getInstance().error(message)

#define LOG_FATAL(message) Log::getInstance().fatal(message)

#endif // LOG_H