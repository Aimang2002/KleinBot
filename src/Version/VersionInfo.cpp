#include "VersionInfo.h"

#include <iomanip>
#include <sstream>
#include <cstring>

#include "KleinVersion.h"

// __GLIBC__ 由 glibc 的标准头定义，必须在包含标准头之后再判断
#if defined(__GLIBC__)
#include <gnu/libc-version.h>
#endif

namespace VersionInfo
{
std::string formatBuildTime(const char *date, const char *time)
{
    if (date == nullptr || time == nullptr)
        return {};
    // __DATE__ 固定为 "Mmm dd yyyy"，日不足两位时带前导空格（如 "Aug  5 2026"）
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    std::istringstream dateStream(date);
    std::string month;
    int day = 0;
    int year = 0;
    if (!(dateStream >> month >> day >> year))
        return std::string(date) + " " + time;

    int monthIndex = 0;
    while (monthIndex < 12 && month != months[monthIndex])
        ++monthIndex;
    if (monthIndex >= 12)
        return std::string(date) + " " + time;

    std::ostringstream result;
    result << year << '-'
           << std::setw(2) << std::setfill('0') << (monthIndex + 1) << '-'
           << std::setw(2) << std::setfill('0') << day
           << ' ' << time;
    return result.str();
}

std::string libcVersion()
{
#if defined(__GLIBC__)
    const char *version = gnu_get_libc_version();
    if (version != nullptr && version[0] != '\0')
        return version;
#endif
    return "N/A";
}

std::string summary()
{
    // 标签按显示宽度对齐（中文与全角冒号占 2 个半角宽，KleinBot版本：共 14 个半角宽，
    // 其余标签用半角空格补齐到同一列）
    std::ostringstream stream;
    stream << "KleinBot版本：" << KLEINBOT_VERSION_STRING << "\n"
           << "    Git 提交：" << KLEINBOT_GIT_HASH << "\n"
           << "    构建时间：" << formatBuildTime(__DATE__, __TIME__) << "\n"
           << "    构建类型：" << KLEINBOT_BUILD_TYPE << "\n"
           << "      编译器：" << KLEINBOT_COMPILER << "\n"
           << "   libc 版本：" << libcVersion() << "\n"
           << "        架构：" << KLEINBOT_ARCH;
    return stream.str();
}
} // namespace VersionInfo
