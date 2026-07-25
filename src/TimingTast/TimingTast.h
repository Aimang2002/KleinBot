#ifndef TIMINGTAST_H
#define TIMINGTAST_H

#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <memory>
#include <unistd.h>
#include "../Log/Log.h"
#include "../ModelApiCaller/OpenAIStandard/OpenAIStandard.h"

// 已到期事件，由 popDueEvent 原子取出
struct DueEvent
{
    uint64_t user_id;
    std::string content;
};

class TimingTast
{
public:
    static TimingTast &getInstance();
    // 设置定时
    std::string setFixedRemind(std::string, uint64_t);
    // 获取当前时间
    uint64_t getPresentTime();
    // 时间转换
    uint64_t timeChange(std::string);
    // 原子取出首个已到期事件（检查+取+删单次加锁），无到期事件返回 nullopt
    std::optional<DueEvent> popDueEvent(uint64_t now);

private:
    TimingTast();
    TimingTast(const TimingTast &) = delete;
    TimingTast &operator=(const TimingTast &) = delete;

    ~TimingTast();

private:
    std::map<uint64_t, std::pair<uint64_t, std::string>> Event; // key = 触发时间戳, value = (QQ号, 提醒内容)
    std::mutex event_mutex;                                     // 保护 Event：setFixedRemind（工作线程）与 popDueEvent（pollingThread）并发访问
};

#endif // TIMINGTAST_H
