#include "WebSearchRouting.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace
{
std::tm localTimeFor(std::time_t value)
{
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &value);
#else
    localtime_r(&value, &localTime);
#endif
    return localTime;
}

std::string formatIsoDate(const std::tm &localTime)
{
    std::ostringstream output;
    output << std::put_time(&localTime, "%Y-%m-%d");
    return output.str();
}
}

std::string currentLocalDateIso()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    return formatIsoDate(localTimeFor(now));
}
