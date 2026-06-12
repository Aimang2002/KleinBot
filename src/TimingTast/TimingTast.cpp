#include "TimingTast.h"

TimingTast &TimingTast::getInstance()
{
    static TimingTast instance;
    return instance;
}

TimingTast::TimingTast()
{
}

uint64_t TimingTast::timeChange(std::string time)
{
    int year = 0;
    int moon = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    bool hasDate = false;

    // 判断是否符合格式
    try
    {
        std::string str;
        if (time.size() > 22)
        {
            LOG_ERROR("非法的时间格式");
            return 0;
        }
        else if (time.size() > 12)
        {
            hasDate = true;
            year = atoi(time.substr(0, time.find("年")).c_str());
            time.erase(0, time.find("年") + 2);

            str = time.substr(0, time.find("月"));
            str.erase(str.begin());
            moon = atoi(str.c_str());
            time.erase(0, time.find("月") + 2);

            str = time.substr(0, time.find("日"));
            str.erase(str.begin());
            day = atoi(str.c_str());
            time.erase(0, time.find("日") + 2);

            str = time.substr(0, time.find(":"));
            str.erase(str.begin());
            hour = atoi(str.c_str());
            time.erase(0, time.find(":") + 1);

            str = time.substr(0, time.find(":"));
            minute = atoi(str.c_str());
        }
        else if (time.size() < 12 && time.size() > 0)
        {
            str = time.substr(0, time.find(":"));
            str.erase(str.begin());
            hour = atoi(str.c_str());
            time.erase(0, time.find(":") + 1);
            str = time.substr(0, time.find(":"));
            minute = atoi(str.c_str());
        }

        // 基于当前本地时间构造目标时间，mktime 自动处理闰年/大小月，不再硬编码年份
        std::time_t now = std::time(nullptr);
        std::tm target = *std::localtime(&now);

        if (hasDate)
        {
            if (moon < 1 || moon > 12 || day < 1 || day > 31)
            {
                LOG_ERROR("非法的日期");
                return 0;
            }
            target.tm_year = year - 1900;
            target.tm_mon = moon - 1;
            target.tm_mday = day;
        }
        // 短格式（只有 时:分）：日期沿用今天

        if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
        {
            LOG_ERROR("非法的时间");
            return 0;
        }
        target.tm_hour = hour;
        target.tm_min = minute;
        target.tm_sec = 0;

        std::time_t time_stamp = std::mktime(&target);
        if (time_stamp == (std::time_t)-1)
        {
            LOG_ERROR("时间转换失败");
            return 0;
        }
        if ((uint64_t)time_stamp <= getPresentTime())
        {
            LOG_ERROR("目标时间已经过去");
            return 0;
        }

        return (uint64_t)time_stamp;
    }
    catch (const std::exception &e)
    {
        std::cerr << "捕获到异常: " << e.what() << std::endl;
        return 0;
    }
}

uint64_t TimingTast::getPresentTime()
{
    // 获取当前时间戳（单位：秒）
    auto now = std::chrono::system_clock::now();
    time_t now_c = std::chrono::system_clock::to_time_t(now);
    return (uint64_t)now_c;
}

std::string TimingTast::setFixedRemind(std::string message, uint64_t user_id)
{
    message.erase(0, message.find("20"));
    uint64_t time_stamp = timeChange(message.substr(0, message.find("/")));
    if (time_stamp == 0)
    {
        return "时间格式不合法或目标时间已过去，设置失败...";
    }

    // 事件注册
    std::lock_guard<std::mutex> locker(event_mutex);
    this->Event.insert(std::make_pair(time_stamp, std::pair<uint64_t, std::string>(user_id, message.substr(message.find("/") + 1))));

    return "设置成功！";
}

std::optional<DueEvent> TimingTast::popDueEvent(uint64_t now)
{
    std::lock_guard<std::mutex> locker(event_mutex);
    if (Event.empty() || Event.begin()->first > now)
    {
        return std::nullopt;
    }
    DueEvent due{Event.begin()->second.first, Event.begin()->second.second};
    Event.erase(Event.begin());
    return due;
}

TimingTast::~TimingTast()
{
}
