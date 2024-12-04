#ifndef TIMINGTAST_H
#define TIMINGTAST_H

#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <map>
#include <thread>
#include <unistd.h>
#include "../Log/Log.h"
#include "../ConfigManager/ConfigManager.h"
#include "../ModelApiCaller/OpenAIStandard/OpenAIStandard.h"

class TimingTast
{
public:
    TimingTast();
    // 设置定时
    std::string setFixedRemind(std::string, uint64_t);
    // 获取当前时间
    uint64_t getPresentTime();
    // 时间转换
    uint64_t timeChange(std::string);
    // 每日提醒
    void dailyRemind(); // 一天的时间戳是86400s
    ~TimingTast();

public:
    std::map<uint64_t, std::pair<uint64_t, std::string>> *Event; // first = 时间戳 pair.first = QQ号 second = 提醒内容
    // ConnectPythonProcess *CPY;
};

#endif // TIMINGTAST_H