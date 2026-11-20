#include <gtest/gtest.h>

#include "Affect/FactsCard.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace
{
// 测试内固定时区（TZ + tzset），结束时恢复原值
class ScopedTimeZone
{
public:
    explicit ScopedTimeZone(const char *zone)
    {
        if (const char *current = std::getenv("TZ"))
            previous = std::string(current);
        setenv("TZ", zone, 1);
        tzset();
    }
    ~ScopedTimeZone()
    {
        if (previous)
            setenv("TZ", previous->c_str(), 1);
        else
            unsetenv("TZ");
        tzset();
    }

private:
    std::optional<std::string> previous;
};

class FactsDirectory
{
public:
    FactsDirectory()
    {
        std::string pattern = "/tmp/kleinbot-facts-XXXXXX";
        pattern.push_back('\0');
        char *created = mkdtemp(pattern.data());
        if (created != nullptr)
            directory = created;
    }
    ~FactsDirectory()
    {
        if (!directory.empty())
            std::filesystem::remove_all(directory);
    }
    const std::string &path() const { return directory; }

private:
    std::string directory;
};

void writeFile(const std::string &path, const std::string &content)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
}

// 每个多字节序列都完整（首字节后跟足够的续字节）
bool isCompleteUtf8(const std::string &text)
{
    for (std::size_t index = 0; index < text.size();)
    {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 1;
        if ((lead & 0x80) == 0)
            length = 1;
        else if ((lead & 0xE0) == 0xC0)
            length = 2;
        else if ((lead & 0xF0) == 0xE0)
            length = 3;
        else if ((lead & 0xF8) == 0xF0)
            length = 4;
        else
            return false;
        if (index + length > text.size())
            return false;
        for (std::size_t offset = 1; offset < length; ++offset)
            if ((static_cast<unsigned char>(text[index + offset]) & 0xC0) != 0x80)
                return false;
        index += length;
    }
    return true;
}
} // namespace

// 现场事实：运行机器本地时间的日期、星期、时刻（月、日不补零，时刻补零）
TEST(FactsCardTest, SceneNoteUsesLocalDateWeekdayAndClock)
{
    ScopedTimeZone zone("Asia/Shanghai");
    // 2026-11-16 07:51:30 UTC = 北京时间 15:51:30，星期一
    EXPECT_EQ(affect::sceneFactsNote(1794815490),
              "现在是 2026年11月16日 星期一 15:51。这一轮你能用的工具，就是提供给你的这些。");
    // 2026-01-04 01:05:00 UTC = 北京时间 09:05，星期日
    EXPECT_EQ(affect::sceneFactsNote(1767488700),
              "现在是 2026年1月4日 星期日 09:05。这一轮你能用的工具，就是提供给你的这些。");
}

// 部署者事实段：去首尾空白；空或全是空白就不加段
TEST(FactsCardTest, DeployerSectionTrimsAndSkipsBlank)
{
    EXPECT_EQ(affect::deployerFactsSection(""), "");
    EXPECT_EQ(affect::deployerFactsSection(" \n\t \r\n"), "");
    EXPECT_EQ(affect::deployerFactsSection("\n  你的声音来自语音合成。\n你的来历没有记录。\n\n"),
              "\n\n[关于你的事实] 由部署者填写，是真实情况：\n你的声音来自语音合成。\n你的来历没有记录。");
}

// 每次现读：改了文件下一次就是新内容；缺失、删掉都返回空串，不报错
TEST(FactsCardTest, ReadsFileFreshEachTimeAndMissingIsEmpty)
{
    FactsDirectory directory;
    ASSERT_FALSE(directory.path().empty());
    const std::string path = directory.path() + "/facts.md";
    affect::DeployerFacts facts(path);
    EXPECT_EQ(facts.path(), path);
    EXPECT_EQ(facts.read(), "") << "文件缺失：空串";

    writeFile(path, "第一版");
    EXPECT_EQ(facts.read(), "第一版");
    writeFile(path, "第二版\n");
    EXPECT_EQ(facts.read(), "第二版\n") << "改了文件，下一次读就是新内容";
    writeFile(path, "   \n\n");
    EXPECT_EQ(affect::deployerFactsSection(facts.read()), "") << "只有空白：不加段";
    writeFile(path, "");
    EXPECT_EQ(facts.read(), "");

    std::filesystem::remove(path);
    EXPECT_EQ(facts.read(), "") << "删掉之后又是空串";
}

// 资源护栏：超过 4096 字节只取前面，按 UTF-8 边界截断，不劈开汉字
TEST(FactsCardTest, OversizedFileIsTruncatedOnUtf8Boundary)
{
    FactsDirectory directory;
    ASSERT_FALSE(directory.path().empty());
    const std::string path = directory.path() + "/facts.md";
    affect::DeployerFacts facts(path);

    // 2 + 3×2000 字节：第 4096 字节落在第 1365 个汉字中间
    std::string content = "ab";
    for (int index = 0; index < 2000; ++index)
        content += "中";
    writeFile(path, content);
    const std::string truncated = facts.read();
    EXPECT_EQ(truncated.size(), 4094U) << "退到那个汉字之前：2 + 3×1364";
    EXPECT_EQ(truncated, content.substr(0, 4094));
    EXPECT_TRUE(isCompleteUtf8(truncated));
    EXPECT_TRUE(isCompleteUtf8(facts.read())) << "同一大小再读（只告警一次）结果相同";

    writeFile(path, std::string(4096, 'x'));
    EXPECT_EQ(facts.read().size(), 4096U) << "正好 4096 字节不截断";
    writeFile(path, std::string(4097, 'y'));
    EXPECT_EQ(facts.read(), std::string(4096, 'y'));
}

// 一轮的事实卡：affect 关着或没接线时什么都不加、不读文件；开着时现场事实总有，
// 部署者事实段按文件内容决定
TEST(FactsCardTest, CardIsEmptyWhenAffectOffOrNotWired)
{
    FactsDirectory directory;
    ASSERT_FALSE(directory.path().empty());
    const std::string path = directory.path() + "/facts.md";
    writeFile(path, "你的声音来自语音合成。");
    affect::DeployerFacts facts(path);
    const std::time_t now = 1794815490;

    const affect::FactsCard off = affect::factsCardFor(false, &facts, now);
    EXPECT_TRUE(off.systemSection.empty());
    EXPECT_TRUE(off.sceneNote.empty());
    const affect::FactsCard unwired = affect::factsCardFor(true, nullptr, now);
    EXPECT_TRUE(unwired.systemSection.empty());
    EXPECT_TRUE(unwired.sceneNote.empty());

    const affect::FactsCard on = affect::factsCardFor(true, &facts, now);
    EXPECT_EQ(on.systemSection, affect::deployerFactsSection("你的声音来自语音合成。"));
    EXPECT_EQ(on.sceneNote, affect::sceneFactsNote(now));

    writeFile(path, "\n");
    const affect::FactsCard blank = affect::factsCardFor(true, &facts, now);
    EXPECT_TRUE(blank.systemSection.empty()) << "facts.md 为空：不加部署者事实段";
    EXPECT_EQ(blank.sceneNote, affect::sceneFactsNote(now)) << "现场事实由程序算，照常有";
}
