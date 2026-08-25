#ifndef VERSIONINFO_H
#define VERSIONINFO_H

/*
 *  版本信息：消费 KleinVersion.h 中的 CMake 生成宏，
 *  供 --version / -V 输出使用（可执行文件名不含版本号）
 */
#include <string>

namespace VersionInfo
{
// 把 __DATE__（"Aug 25 2026"）与 __TIME__（"20:45:12"）重排为 "2026-08-25 20:45:12"；
// 输入为空或格式异常时原样拼接返回
std::string formatBuildTime(const char *date, const char *time);

// 运行时 libc 版本（glibc 返回如 "2.35"；其他平台无标准查询接口，返回 "N/A"）
std::string libcVersion();

// --version / -V 打印的完整版本信息（多行）
std::string summary();
}

#endif // VERSIONINFO_H
