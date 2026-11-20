#include <gtest/gtest.h>

#include "Asset/ImagePlaceholder.h"
#include "Perception/GroupLineFormat.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

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

// 2026-11-16 15:49 北京时间起，每条隔一分钟
constexpr std::int64_t kAt1549 = 1794815340;

GroupMessageRecord message(const std::string &messageId, const std::string &nickname,
                           const std::string &text, std::int64_t ts,
                           const std::string &replyTo = "", bool isSelf = false)
{
    GroupMessageRecord record;
    record.groupId = 8823;
    record.userId = isSelf ? 10086 : 42;
    record.nickname = nickname;
    record.text = text;
    record.timestamp = ts;
    record.isSelf = isSelf;
    record.messageId = messageId;
    record.replyToMessageId = replyTo;
    return record;
}

BotIdentity klein()
{
    BotIdentity bot;
    bot.id = 10086;
    bot.name = "Klein";
    return bot;
}
} // namespace

// 没有引用：与此前各处手拼的 "[HH:MM] 名字: 正文" 逐字相同（她自己写 名字（你））
TEST(GroupLineFormatTest, LineWithoutReplyMatchesLegacyFormat)
{
    const GroupMessageRecord other = message("m1", "阿青", "我们部门被边缘化了", kAt1549);
    const GroupMessageRecord self = message("m2", "Klein", "闲下来也挺好", kAt1549 + 60, "", true);
    const GroupReplyIndex replies({other, self});
    EXPECT_EQ(formatGroupLine(klein(), other, replies),
              "[" + groupClock(other.timestamp) + "] 阿青: 我们部门被边缘化了");
    EXPECT_EQ(formatGroupLine(klein(), self, replies),
              "[" + groupClock(self.timestamp) + "] Klein（你）: 闲下来也挺好");
}

// 回复她：写“你”；她回复别人：写对方昵称；时刻是本地 HH:MM
TEST(GroupLineFormatTest, ReplyShowsWhoAndWhichLine)
{
    ScopedTimeZone zone("Asia/Shanghai");
    const std::vector<GroupMessageRecord> records{
        message("m1", "阿青", "我们部门被边缘化了", kAt1549),
        message("m2", "Klein", "闲下来也挺好，摸鱼不用愧疚", kAt1549 + 60, "m1", true),
        message("m3", "阿青", "也是", kAt1549 + 120, "m2"),
    };
    const GroupReplyIndex replies(records);
    EXPECT_EQ(formatGroupLine(klein(), records[1], replies),
              "[15:50] Klein（你）（回复 阿青：「我们部门被边缘化了」）: 闲下来也挺好，摸鱼不用愧疚");
    EXPECT_EQ(formatGroupLine(klein(), records[2], replies),
              "[15:51] 阿青（回复 你：「闲下来也挺好，摸鱼不用愧…」）: 也是");
}

// 引语：前 12 个码点，正好 12 个不加“…”，多一个就加；空白折成一个空格；多字节安全
TEST(GroupLineFormatTest, QuoteKeepsTwelveCodepoints)
{
    EXPECT_EQ(replyQuoteOf("一二三四五六七八九十甲乙"), "「一二三四五六七八九十甲乙」");
    EXPECT_EQ(replyQuoteOf("一二三四五六七八九十甲乙丙"), "「一二三四五六七八九十甲乙…」");
    EXPECT_EQ(replyQuoteOf("abcdefghijklmnop"), "「abcdefghijkl…」");
    EXPECT_EQ(replyQuoteOf("  第一行\n\n第二行  "), "「第一行 第二行」");
    EXPECT_EQ(replyQuoteOf("emoji😀试一下"), "「emoji😀试一下」");
    EXPECT_EQ(replyQuoteOf("一二三四五六七八九十甲 乙丙"), "「一二三四五六七八九十甲…」")
        << "截断处的空格不留在“…”前";
}

// 被回复的是图片、表情等非文字内容：写短词，不抄 asset_id；文字里夹的图片换成 [图片]
TEST(GroupLineFormatTest, PlaceholderQuoteUsesShortWordWithoutAssetId)
{
    const std::string image = formatImagePlaceholder("gimg_abc123", "inbound");
    const std::string sticker = formatImagePlaceholder("gimg_def456", "inbound", true);
    const std::string captioned = attachImageCaption(image, "gimg_abc123", "照片：一只猫");
    ASSERT_FALSE(captioned.empty());

    EXPECT_EQ(replyQuoteOf(image), "一张图片");
    EXPECT_EQ(replyQuoteOf(captioned), "一张图片");
    EXPECT_EQ(replyQuoteOf(sticker), "一张表情包");
    EXPECT_EQ(replyQuoteOf(formatImagePlaceholder("gimg_x", "generated")), "一张图片");
    EXPECT_EQ(replyQuoteOf("[图片]"), "一张图片");
    EXPECT_EQ(replyQuoteOf("[表情 狗头]"), "一个表情");
    EXPECT_EQ(replyQuoteOf("[语音]"), "一段语音");
    EXPECT_EQ(replyQuoteOf(image + " " + sticker), "一张图片等");
    EXPECT_EQ(replyQuoteOf("看这个 " + captioned), "「看这个 [图片]」");
    EXPECT_EQ(replyQuoteOf("[@Klein]"), "「[@Klein]」") << "不认识的标签按文字引用";

    const std::vector<GroupMessageRecord> records{
        message("m1", "小白", captioned, kAt1549),
        message("m2", "老余", "哈哈哈", kAt1549 + 60, "m1"),
    };
    const GroupReplyIndex replies(records);
    const std::string line = formatGroupLine(klein(), records[1], replies);
    EXPECT_EQ(line, "[" + groupClock(records[1].timestamp) + "] 老余（回复 小白：一张图片）: 哈哈哈");
    EXPECT_EQ(line.find("asset_id"), std::string::npos);
}

// 被回复的消息不在索引里（已过期、更早）：如实说是一条更早的消息
TEST(GroupLineFormatTest, MissingTargetSaysEarlierMessage)
{
    const std::vector<GroupMessageRecord> records{
        message("m5", "小白", "对", kAt1549, "m999"),
        message("", "Klein", "回执还没回来的自消息", kAt1549 + 60, "", true),
    };
    const GroupReplyIndex replies(records);
    EXPECT_EQ(formatGroupLine(klein(), records[0], replies),
              "[" + groupClock(records[0].timestamp) + "] 小白（回复了一条更早的消息）: 对");
    EXPECT_EQ(replies.find(""), nullptr) << "没有 message_id 的记录不进索引";
    EXPECT_EQ(replies.find("m999"), nullptr);
    ASSERT_NE(replies.find("m5"), nullptr);
    EXPECT_EQ(replies.find("m5")->quote, "「对」");
}
