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
    std::string setFixedRemind(std::string, UINT64);
    // 获取当前时间
    UINT64 getPresentTime();
    // 时间转换
    UINT64 timeChange(std::string);
    // 每日提醒
    void dailyRemind(); // 一天的时间戳是86400s
    ~TimingTast();

public:
    std::map<UINT64, std::pair<UINT64, std::string>> *Event; // first = 时间戳 pair.first = QQ号 second = 提醒内容
    // ConnectPythonProcess *CPY;
};

#endif // TIMINGTAST_H