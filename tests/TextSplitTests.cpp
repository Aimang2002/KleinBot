#include <gtest/gtest.h>

#include "utils/TextSplit.h"

#include <string>
#include <vector>

namespace
{
std::string join(const std::vector<std::string> &parts, const std::string &sep = "|")
{
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        if (i > 0)
            out += sep;
        out += parts[i];
    }
    return out;
}
} // namespace

TEST(TextSplitTest, NewlineSplitsIntoMessagesAndDropsBlankLines)
{
    auto parts = splitTextSegments("一句。\n第二句。\n\n   \n第三句。", 5000);
    ASSERT_EQ(parts.size(), 3U);
    EXPECT_EQ(parts[0], "一句。");
    EXPECT_EQ(parts[1], "第二句。");
    EXPECT_EQ(parts[2], "第三句。");
}

TEST(TextSplitTest, CodeBlockStaysWhole)
{
    auto parts = splitTextSegments("看这个：\n```cpp\nint a;\nint b;\n```", 5000);
    ASSERT_EQ(parts.size(), 2U);
    EXPECT_EQ(parts[0], "看这个：");
    EXPECT_EQ(parts[1], "```cpp\nint a;\nint b;\n```");
}

TEST(TextSplitTest, UnclosedFenceFlushesAccumulatedContent)
{
    auto parts = splitTextSegments("```\n只有一半", 5000);
    ASSERT_EQ(parts.size(), 1U);
    EXPECT_EQ(parts[0], "```\n只有一半");
}

TEST(TextSplitTest, OversizedSegmentHardSplitByUtf8Chars)
{
    const std::string longText = "一二三四五六七";
    auto parts = splitTextSegments(longText, 3, 100);
    ASSERT_EQ(parts.size(), 3U);
    EXPECT_EQ(parts[0], "一二三");
    EXPECT_EQ(parts[1], "四五六");
    EXPECT_EQ(parts[2], "七");
    // 多字节字符不被截断
    EXPECT_EQ(join(parts, ""), longText);
}

TEST(TextSplitTest, SegmentCapMergesOverflowIntoLast)
{
    std::string text;
    for (int i = 1; i <= 15; ++i)
        text += "第" + std::to_string(i) + "条\n";
    auto parts = splitTextSegments(text, 5000, 10);
    ASSERT_EQ(parts.size(), 10U);
    EXPECT_EQ(parts[0], "第1条");
    EXPECT_NE(parts[9].find("第10条"), std::string::npos);
    EXPECT_NE(parts[9].find("第15条"), std::string::npos) << "溢出行并入最后一段";
}

TEST(TextSplitTest, TrailingNewlineAndCrlfAreClean)
{
    auto parts = splitTextSegments("甲乙\r\n丙\n", 5000);
    ASSERT_EQ(parts.size(), 2U);
    EXPECT_EQ(parts[0], "甲乙");
    EXPECT_EQ(parts[1], "丙");
}

TEST(TextSplitTest, EmptyAndWhitespaceOnlyYieldNothing)
{
    EXPECT_TRUE(splitTextSegments("", 5000).empty());
    EXPECT_TRUE(splitTextSegments("\n \n\t\n", 5000).empty());
}

TEST(TextSplitTest, WholeKeepsNewlinesAsSingleMessage)
{
    const std::string text = "第一段。\n第二段。\n\n```cpp\nint a;\n```";
    auto parts = splitTextWhole(text, 5000);
    ASSERT_EQ(parts.size(), 1U);
    EXPECT_EQ(parts[0], text);
}

TEST(TextSplitTest, WholeHardSplitsOnlyWhenOverLimit)
{
    const std::string longText = "甲乙丙\n丁戊己";
    auto parts = splitTextWhole(longText, 4);
    ASSERT_EQ(parts.size(), 2U);
    EXPECT_EQ(parts[0], "甲乙丙\n");
    EXPECT_EQ(parts[1], "丁戊己");
    EXPECT_EQ(join(parts, ""), longText);
}

TEST(TextSplitTest, WholeEmptyYieldsNothing)
{
    EXPECT_TRUE(splitTextWhole("", 5000).empty());
}
