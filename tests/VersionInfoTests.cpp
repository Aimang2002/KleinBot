#include <gtest/gtest.h>

#include <string>

#include "KleinVersion.h"
#include "Version/VersionInfo.h"

TEST(FormatBuildTimeTest, RearrangesCompilerDateAndTime)
{
    EXPECT_EQ(VersionInfo::formatBuildTime("Aug 25 2026", "20:45:12"), "2026-08-25 20:45:12");
    EXPECT_EQ(VersionInfo::formatBuildTime("Jan  1 2027", "00:00:00"), "2027-01-01 00:00:00");
    EXPECT_EQ(VersionInfo::formatBuildTime("Dec 31 2026", "23:59:59"), "2026-12-31 23:59:59");
}

TEST(FormatBuildTimeTest, FallsBackToRawInputOnInvalidFormat)
{
    EXPECT_EQ(VersionInfo::formatBuildTime("NotADate", "12:00:00"), "NotADate 12:00:00");
    EXPECT_EQ(VersionInfo::formatBuildTime("Foo 25 2026", "12:00:00"), "Foo 25 2026 12:00:00");
    EXPECT_EQ(VersionInfo::formatBuildTime(nullptr, "12:00:00"), "");
    EXPECT_EQ(VersionInfo::formatBuildTime("Aug 25 2026", nullptr), "");
}

TEST(LibcVersionTest, ReportsNonEmptyVersion)
{
    EXPECT_FALSE(VersionInfo::libcVersion().empty());
}

TEST(VersionSummaryTest, ContainsAllBuildMetadataFields)
{
    const std::string summary = VersionInfo::summary();

    EXPECT_NE(summary.find(std::string("KleinBot版本：") + KLEINBOT_VERSION_STRING), std::string::npos);
    EXPECT_NE(summary.find(std::string("    Git 提交：") + KLEINBOT_GIT_HASH), std::string::npos);
    EXPECT_NE(summary.find(std::string("    构建类型：") + KLEINBOT_BUILD_TYPE), std::string::npos);
    EXPECT_NE(summary.find(std::string("      编译器：") + KLEINBOT_COMPILER), std::string::npos);
    EXPECT_NE(summary.find(std::string("   libc 版本：") + VersionInfo::libcVersion()), std::string::npos);
    EXPECT_NE(summary.find(std::string("        架构：") + KLEINBOT_ARCH), std::string::npos);
    EXPECT_NE(summary.find("    构建时间："), std::string::npos);
}
