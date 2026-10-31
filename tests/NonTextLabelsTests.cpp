#include <gtest/gtest.h>

#include "Application/NonTextLabels.h"

TEST(NonTextLabelsTest, FacesStripBracketsAndAttachmentsKeepOrder)
{
    InboundMessage message;
    message.face_summaries = {"[狗头]", "face:14", "[]", ""};
    message.attachment_labels = {"[视频]", "", "[文件 a.png]"};

    const auto withFaces = nonTextLabels(message, true);
    ASSERT_EQ(withFaces.size(), 4U);
    EXPECT_EQ(withFaces[0], "[表情 狗头]");
    EXPECT_EQ(withFaces[1], "[表情 face:14]");
    EXPECT_EQ(withFaces[2], "[视频]");
    EXPECT_EQ(withFaces[3], "[文件 a.png]");

    const auto withoutFaces = nonTextLabels(message, false);
    ASSERT_EQ(withoutFaces.size(), 2U);
    EXPECT_EQ(withoutFaces[0], "[视频]");
}

TEST(NonTextLabelsTest, JoinAndAppendMatchHistoricalSpacing)
{
    EXPECT_EQ(joinLabels({}), "");
    EXPECT_EQ(joinLabels({"[视频]", "[卡片]"}), "[视频] [卡片]");

    std::string empty;
    appendLabels(empty, {"[视频]", "[卡片]"});
    EXPECT_EQ(empty, "[视频] [卡片]");

    std::string text = "看这个";
    appendLabels(text, {"[卡片 某视频]"});
    EXPECT_EQ(text, "看这个 [卡片 某视频]");
}
