#include <gtest/gtest.h>

#include "Asset/ImagePlaceholder.h"

#include <string>

// 占位符格式是会话历史、群缓冲、瞥视写回与模型规则文本共用的契约：逐字节钉死
TEST(ImagePlaceholderTest, FormatsInboundGeneratedAndStickerVariants)
{
    EXPECT_EQ(formatImagePlaceholder("img_1", "inbound"), "[image asset_id=img_1 source=inbound]");
    EXPECT_EQ(formatImagePlaceholder("gimg_2", "inbound", true),
              "[image asset_id=gimg_2 source=inbound sticker]");
    EXPECT_EQ(formatImagePlaceholder("gimg_3", "generated"),
              "[image asset_id=gimg_3 source=generated]");
    EXPECT_EQ(formatImagePlaceholder("gimg_3", "generated", false),
              formatImagePlaceholder("gimg_3", "generated"));
}

TEST(ImagePlaceholderTest, AttachCaptionRoundTripsWithFormatter)
{
    const std::string placeholder = formatImagePlaceholder("gimg_1", "inbound");
    EXPECT_EQ(attachImageCaption(placeholder, "gimg_1", "照片：猫"),
              "[image asset_id=gimg_1 source=inbound | 照片：猫]");

    // 前后文保留，贴纸标记之后插入
    const std::string text = "看 " + formatImagePlaceholder("gimg_1", "inbound", true) + " 哈哈";
    EXPECT_EQ(attachImageCaption(text, "gimg_1", "表情包：狗"),
              "看 [image asset_id=gimg_1 source=inbound sticker | 表情包：狗] 哈哈");
}

TEST(ImagePlaceholderTest, AttachCaptionRequiresExactAssetId)
{
    EXPECT_TRUE(attachImageCaption(formatImagePlaceholder("gimg_12", "inbound"), "gimg_1", "x").empty());
    EXPECT_TRUE(attachImageCaption(formatImagePlaceholder("gimg_1", "inbound"), "gimg_12", "x").empty());
    // 同一文本里有两张图时只改指定那张
    const std::string two = formatImagePlaceholder("gimg_12", "inbound") + " " +
                            formatImagePlaceholder("gimg_1", "inbound");
    EXPECT_EQ(attachImageCaption(two, "gimg_12", "x"),
              "[image asset_id=gimg_12 source=inbound | x] [image asset_id=gimg_1 source=inbound]");
}

TEST(ImagePlaceholderTest, AttachCaptionIsNoOpWhenAlreadyCaptionedOrNothingToDo)
{
    const std::string captioned = attachImageCaption(formatImagePlaceholder("gimg_1", "inbound"),
                                                     "gimg_1", "旧");
    ASSERT_FALSE(captioned.empty());
    EXPECT_TRUE(attachImageCaption(captioned, "gimg_1", "新").empty());
    EXPECT_TRUE(attachImageCaption("[图片]", "gimg_1", "x").empty());
    EXPECT_TRUE(attachImageCaption(formatImagePlaceholder("gimg_1", "inbound"), "gimg_1", "").empty());
    EXPECT_TRUE(attachImageCaption(formatImagePlaceholder("gimg_1", "inbound"), "", "x").empty());
}
