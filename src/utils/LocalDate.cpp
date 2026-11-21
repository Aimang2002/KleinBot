#include "LocalDate.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace
{
std::string formatIsoDate(const std::tm &localTime)
{
    std::ostringstream output;
    output << std::put_time(&localTime, "%Y-%m-%d");
    return output.str();
}
}

std::tm localTimeOf(std::time_t value)
{
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &value);
#else
    localtime_r(&value, &localTime);
#endif
    return localTime;
}

std::time_t localDayStart(std::time_t value)
{
    std::tm local = localTimeOf(value);
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1; // 夏令时由 mktime 自行判定
    return std::mktime(&local);
}

int currentLocalHour()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    return localTimeOf(now).tm_hour;
}

std::string currentLocalDateIso()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    return formatIsoDate(localTimeOf(now));
}
