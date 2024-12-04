#include "TimingTast.h"

TimingTast::TimingTast()
{
    this->Event = new std::map<uint64_t, std::pair<uint64_t, std::string>>();
}

uint64_t TimingTast::timeChange(std::string time)
{
    int year = 0;
    int moon = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;

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

        // 判断时间是否合法
        uint64_t time_stamp = 0; // 单位：s
        int dayArray[12] = {0, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (year > 2025 || year < 2023)
        {
            LOG_ERROR("时间过长或过短");
            return 0;
        }
        else if (year == 2023)
        {
            time_stamp = 1672502400;
        }
        else if (year == 2024)
        {
            time_stamp = 1704038400;
            if (moon > 2)
                time_stamp += 86400; // 闰年2月
        }

        for (int i = 1; i < moon; i++)
        {
            day += dayArray[i];
        }
        day--; // 当前天数并不记录为1天
        time_stamp += day * 86400;
        time_stamp += hour * 3600;
        time_stamp += minute * 60;

        return time_stamp;
    }
    catch (const std::exception &e)
    {
        std::cerr << "捕获到异常: " << e.what() << std::endl;
        return -1;
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

    // 事件注册
    this->Event->insert(make_pair(time_stamp, std::pair<uint64_t, std::string>(user_id, message.substr(message.find("/") + 1))));

    return "设置成功！";
}

TimingTast::~TimingTast()
{
    if (this->Event != nullptr)
    {
        delete this->Event;
        this->Event = nullptr;
    }
}