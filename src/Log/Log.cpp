#include "Log.h"
#include <cstring>
#include <sstream>
#include <filesystem>

Log::Log()
{
    maxLength = 1024;
    isRunning = false;
}

Log &Log::getInstance()
{
    static Log instance;
    Log *instancePtr = &instance;
    std::call_once(instance.initFlag, [instancePtr]() {
        instancePtr->initialized = instancePtr->initThread();
    });
    if (!instance.initialized)
    {
        throw std::runtime_error("Log thread init failed");
    }
    return instance;
}

Log::~Log()
{
    shutdown();
}

bool Log::initThread()
{
    // 创建日志文件
    try
    {
        if (!std::filesystem::exists("logs"))
        {
            std::filesystem::create_directory("logs");
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "日志目录创建失败，请尝试手动在当前目录下创建logs文件夹！错误内容：" << e.what() << std::endl;
        return false;
    }

#if defined(__WIN32) || defined(__WIN64)
    logFile.open(std::string("logs\\" + Log::getCurrentTime("y", "-") + ".log"), std::ios::app);
#else
    logFile.open(std::string("logs/" + Log::getCurrentTime("y", "-") + ".log"), std::ios::app);
#endif
    if (!logFile.is_open())
    {
        std::cout << "创建日志文件失败！" << std::endl;
        return false;
    }

    {
        std::lock_guard<std::mutex> locker(mutex_);
        isRunning = true;
    }
    logThread = std::thread([&]() { //
        while (true)
        {
            std::unique_lock<std::mutex> locker(mutex_);
            cv.wait(locker, [&]() { //
                return !messageQueue.empty() || !isRunning;
            });

            if (messageQueue.empty() && !isRunning)
            {
                break;
            }
            std::string msg = messageQueue.front();
            messageQueue.pop();
            locker.unlock();
            record(msg);
        }
        std::cout << "线程优雅的结束了" << std::endl;
    });
    return true;
}

void Log::info(std::string message)
{
    std::cout << "\033[32m" << "[" << getCurrentTime("y+h") << "] " << "INFO: " << messageLengthCheck(message) << "\033[0m\n";
}

void Log::debug(std::string message)
{
#ifdef DEBUG
    std::cout << "\033[38;5;208m" << "[" << getCurrentTime("y+h") << "] " << "DEBUG: " << messageLengthCheck(message) << "\033[0m\n";
#endif
}

void Log::warning(std::string message)
{
    std::string result = messageLengthCheck(message);
    result.insert(0, "[" + getCurrentTime("y+h") + "] " + "WARNING: ");
    std::cout << "\033[33m" << result << "\033[0m\n";
    addMessage(result);
}

void Log::error(std::string message)
{
    std::string result = messageLengthCheck(message);
    result.insert(0, "[" + getCurrentTime("y+h") + "] " + "ERROR: ");
    addMessage(result);
    std::cerr << "\033[1;31m" << "[" << getCurrentTime("y+h") << "] " << "ERROR: " << result << "\033[0m\n";
}

void Log::fatal(std::string message)
{
    std::string result = messageLengthCheck(message);
    result.insert(0, "[" + getCurrentTime("y+h") + "] " + "FATAL: ");
    addMessage(result);
    std::cerr << "\033[31m" << result << "\033[0m\n";
}

void Log::shutdown()
{
    {
        std::lock_guard<std::mutex> locker(mutex_);
        if (!isRunning)
        {
            return;
        }
        isRunning = false;
    }
    cv.notify_all();
    if (logThread.joinable())
    {
        logThread.join();
    }
    if (logFile.is_open())
    {
        logFile.close();
    }
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

void Log::addMessage(const std::string message)
{
    std::lock_guard<std::mutex> locker(mutex_);
    messageQueue.push(message);
    cv.notify_one(); // 唤醒线程
}

std::string Log::getCurrentTime(std::string ch, std::string split)
{
    time_t now = time(nullptr);
    tm *t = localtime(&now);
    std::stringstream sstring;

    auto pad = [](int i)
    {
        std::string res = std::to_string(i);
        if (i < 10)
        {
            res.insert(0, "0");
        }
        return res;
    };

    if (ch == "y")
    {
        sstring << (1900 + t->tm_year) << split << pad(t->tm_mon + 1) << split << pad(t->tm_mday);
    }
    else if (ch == "y+h")
    {
        sstring << (1900 + t->tm_year) << split << pad(t->tm_mon + 1) << split << pad(t->tm_mday) << " "
                << pad(t->tm_hour) << split << pad(t->tm_min) << split << pad(t->tm_sec);
    }
    else if (ch == "h")
    {
        sstring << pad(t->tm_hour) << split << pad(t->tm_min) << split << pad(t->tm_sec);
    }
    return sstring.str();
}

void Log::record(const std::string message)
{
    if (!logFile.is_open())
    {
        std::cout << "日志信息无法写入！" << std::endl;
        return;
    }
    logFile << message << std::endl;
}
