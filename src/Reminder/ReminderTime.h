#ifndef REMINDER_TIME_H
#define REMINDER_TIME_H

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>

// 提醒时间的纯函数工具：解析/格式化/重复滚动，全部基于本地时区

// 严格解析 "YYYY-MM-DDTHH:MM" 或 "YYYY-MM-DD HH:MM" 为本地时区 unix 秒。
// 字段范围非法（如 2 月 30 日）返回 nullopt；mktime 的归一化会静默滚动
// 越界字段，因此解析后做一次往返比对。
inline std::optional<int64_t> parseIsoLocal(const std::string &value)
{
    if (value.size() != 16)
        return std::nullopt;
    if (value[4] != '-' || value[7] != '-')
        return std::nullopt;
    if (value[10] != 'T' && value[10] != ' ')
        return std::nullopt;
    if (value[13] != ':')
        return std::nullopt;
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        if (index == 4 || index == 7 || index == 10 || index == 13)
            continue;
        if (value[index] < '0' || value[index] > '9')
            return std::nullopt;
    }
    std::tm parsed = {};
    parsed.tm_year = std::stoi(value.substr(0, 4)) - 1900;
    parsed.tm_mon = std::stoi(value.substr(5, 2)) - 1;
    parsed.tm_mday = std::stoi(value.substr(8, 2));
    parsed.tm_hour = std::stoi(value.substr(11, 2));
    parsed.tm_min = std::stoi(value.substr(14, 2));
    parsed.tm_isdst = -1;
    if (parsed.tm_mon < 0 || parsed.tm_mon > 11 ||
        parsed.tm_mday < 1 || parsed.tm_mday > 31 ||
        parsed.tm_hour < 0 || parsed.tm_hour > 23 ||
        parsed.tm_min < 0 || parsed.tm_min > 59)
        return std::nullopt;

    const std::tm original = parsed; // mktime 会原地归一化越界字段，先留副本
    const std::time_t seconds = std::mktime(&parsed);
    if (seconds == static_cast<std::time_t>(-1))
        return std::nullopt;
    std::tm roundTrip = *std::localtime(&seconds);
    if (roundTrip.tm_year != original.tm_year || roundTrip.tm_mon != original.tm_mon ||
        roundTrip.tm_mday != original.tm_mday || roundTrip.tm_hour != original.tm_hour ||
        roundTrip.tm_min != original.tm_min)
        return std::nullopt;
    return static_cast<int64_t>(seconds);
}

// unix 秒 → "YYYY-MM-DD HH:MM"（本地时区展示）
inline std::string formatLocal(int64_t epochSeconds)
{
    const std::time_t seconds = static_cast<std::time_t>(epochSeconds);
    std::tm *local = std::localtime(&seconds);
    char buffer[24];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", local);
    return buffer;
}

inline int64_t nowSeconds()
{
    return static_cast<int64_t>(std::time(nullptr));
}

// 重复提醒滚动到严格晚于 now 的下一次触发时间。
// 用整除一步算出需要跨过的周期数，避免逐周期循环。
inline int64_t nextOccurrenceAfter(int64_t scheduledAt, const std::string &repeat,
                                   int64_t now)
{
    constexpr int64_t dailySeconds = 86400;
    constexpr int64_t weeklySeconds = 604800;
    int64_t period = 0;
    if (repeat == "daily")
        period = dailySeconds;
    else if (repeat == "weekly")
        period = weeklySeconds;
    else
        return 0;
    if (scheduledAt > now)
        return scheduledAt;
    const int64_t periods = (now - scheduledAt) / period + 1;
    return scheduledAt + periods * period;
}

#endif // REMINDER_TIME_H
