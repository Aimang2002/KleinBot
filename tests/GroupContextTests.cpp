#include <gtest/gtest.h>

#include "Perception/GroupContextStore.h"

#include <filesystem>
#include <string>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-groupctx-XXXXXX";
        pattern.push_back('\0');
        char *created = mkdtemp(pattern.data());
        if (created != nullptr)
            directory = created;
    }

    ~TemporaryDirectory()
    {
        if (!directory.empty())
            std::filesystem::remove_all(directory);
    }

    const std::string &path() const { return directory; }

private:
    std::string directory;
};

GroupMessageRecord record(std::uint64_t group, std::uint64_t qq, const std::string &nickname,
                          const std::string &text, std::int64_t ts,
                          bool mentionsBot = false, bool isSelf = false)
{
    GroupMessageRecord value;
    value.groupId = group;
    value.speakerId = "qq-" + std::to_string(qq); // 哈希由调用方负责，测试直接占位
    value.nickname = nickname;
    value.text = text;
    value.timestamp = ts;
    value.mentionsBot = mentionsBot;
    value.isSelf = isSelf;
    return value;
}
} // namespace

TEST(GroupContextStoreTest, AppendSnapshotAndPerGroupCap)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    GroupContextStore store(dbPath);
    ASSERT_TRUE(store.isOpen());

    store.append(record(8823, 10, "小白", "第一条", 1000));
    store.append(record(8823, 20, "小黑", "第二条", 1001));
    store.append(record(9001, 30, "路人", "别的群", 1002));

    auto group8823 = store.snapshot(8823);
    ASSERT_EQ(group8823.size(), 2U);
    EXPECT_EQ(group8823[0].text, "第一条");
    EXPECT_EQ(group8823[1].text, "第二条");
    EXPECT_EQ(group8823[0].seq + 1, group8823[1].seq);
    EXPECT_EQ(store.snapshot(9001).size(), 1U);
    EXPECT_TRUE(store.snapshot(7777).empty());

    // 条数上限 300：第 301 条进入时最旧的被淘汰
    for (int index = 0; index < 298; ++index)
        store.append(record(8823, 10, "小白", "刷屏" + std::to_string(index), 1010 + index));
    EXPECT_EQ(store.snapshot(8823).size(), 300U);
    store.append(record(8823, 10, "小白", "最新", 2000));
    auto capped = store.snapshot(8823);
    ASSERT_EQ(capped.size(), 300U);
    EXPECT_NE(capped.front().text, "第一条") << "最旧应被淘汰";
    EXPECT_EQ(capped.back().text, "最新");
}

TEST(GroupContextStoreTest, TtlPruneRemovesExpiredFromMirrorAndDisk)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";
    {
        GroupContextStore store(dbPath);
        ASSERT_TRUE(store.isOpen());
        store.append(record(8823, 10, "小白", "较早的", 1'000'000));
        store.append(record(8823, 20, "小黑", "较新的", 1'000'000 + 23 * 60 * 60));
        // 都在 24h TTL 内：不清理
        store.prune(1'000'000 + 23 * 60 * 60);
        EXPECT_EQ(store.snapshot(8823).size(), 2U);
    }

    // 重启重建后再清：验证磁盘上的过期行也被删除（而非仅镜像）
    {
        GroupContextStore store(dbPath);
        ASSERT_TRUE(store.isOpen());
        auto rebuilt = store.snapshot(8823);
        ASSERT_EQ(rebuilt.size(), 2U) << "重启重建镜像";
        store.prune(1'000'000 + 25 * 60 * 60); // 较早的已过 24h TTL
        EXPECT_EQ(store.snapshot(8823).size(), 1U);
        EXPECT_EQ(store.snapshot(8823).front().text, "较新的");
    }
    {
        GroupContextStore store(dbPath);
        // 磁盘确实删了：重建后只剩一条
        EXPECT_EQ(store.snapshot(8823).size(), 1U);
    }
}

TEST(GroupContextStoreTest, RestartRebuildsWithFieldsAndSharesSalt)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const std::string dbPath = temporaryDirectory.path() + "/conversation.db";

    std::string qq10Hash;
    {
        GroupContextStore store(dbPath);
        ASSERT_TRUE(store.isOpen());
        qq10Hash = store.speakerHash(10);
        EXPECT_EQ(qq10Hash.size(), 40U);

        GroupMessageRecord inbound;
        inbound.groupId = 8823;
        inbound.speakerId = qq10Hash;
        inbound.nickname = "小白";
        inbound.text = "原神深渊打不过";
        inbound.timestamp = 1000;
        inbound.mentionsBot = true;
        inbound.messageId = "MsajdvLq";
        inbound.replyToMessageId = "7799";
        store.append(inbound);
        store.append(record(8823, 0, "Klein", "[图片]", 1001, false, true));
    }

    {
        GroupContextStore store(dbPath);
        ASSERT_TRUE(store.isOpen());
        EXPECT_EQ(store.speakerHash(10), qq10Hash) << "与 PerceptionStore 同盐同值";

        auto rebuilt = store.snapshot(8823);
        ASSERT_EQ(rebuilt.size(), 2U);
        EXPECT_EQ(rebuilt[0].speakerId, qq10Hash);
        EXPECT_EQ(rebuilt[0].nickname, "小白");
        EXPECT_EQ(rebuilt[0].text, "原神深渊打不过");
        EXPECT_TRUE(rebuilt[0].mentionsBot);
        EXPECT_FALSE(rebuilt[0].isSelf);
        EXPECT_EQ(rebuilt[0].messageId, "MsajdvLq");
        EXPECT_EQ(rebuilt[0].replyToMessageId, "7799");
        EXPECT_TRUE(rebuilt[1].isSelf);
        EXPECT_EQ(rebuilt[1].nickname, "Klein");
    }
}
