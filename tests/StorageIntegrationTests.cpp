#include <gtest/gtest.h>

#include "Asset/ImageAssetStore.h"
#include "Persistence/ConversationStore.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-tests-XXXXXX";
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
}

TEST(ConversationStoreIntegrationTest, PersistsSearchesAndRemovesUserHistory)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const auto databasePath = temporaryDirectory.path() + "/conversation.db";

    ConversationStore store(databasePath);
    const int64_t firstId = store.append(10, "user", "first message", 1000);
    const int64_t secondId = store.append(10, "assistant", "second answer", 1001);
    store.append(20, "user", "other user", 1002);

    EXPECT_GT(firstId, 0);
    EXPECT_GT(secondId, firstId);

    const auto history = store.loadAll(10);
    ASSERT_EQ(history.size(), 2U);
    EXPECT_EQ(history[0].role, "user");
    EXPECT_EQ(history[0].content, "first message");
    EXPECT_EQ(history[1].role, "assistant");

    const auto searchResult = store.search(10, "second");
    ASSERT_EQ(searchResult.size(), 1U);
    EXPECT_EQ(searchResult.front().content, "second answer");

    EXPECT_EQ(store.removeLast(10, 1), secondId);
    ASSERT_EQ(store.loadAll(10).size(), 1U);

    store.clearUser(10);
    EXPECT_TRUE(store.loadAll(10).empty());
    EXPECT_EQ(store.loadAll(20).size(), 1U);
}

TEST(ImageAssetStoreIntegrationTest, SavesFindsAndClearsAssetsWithFiles)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const auto databasePath = temporaryDirectory.path() + "/assets.db";
    const auto assetDirectory = temporaryDirectory.path() + "/images";

    ImageAssetStore store(databasePath, assetDirectory);
    const auto asset = store.saveBase64(10, "aGVsbG8=", "generated", "prompt", 30);
    ASSERT_TRUE(asset.has_value());
    EXPECT_TRUE(std::filesystem::exists(asset->local_path));
    EXPECT_EQ(store.readBase64(*asset), "aGVsbG8=");

    const auto found = store.find(10, asset->asset_id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->prompt, "prompt");
    EXPECT_FALSE(store.find(20, asset->asset_id).has_value());

    store.attachToConversation(10, asset->asset_id, 50);
    const auto attached = store.find(10, asset->asset_id);
    ASSERT_TRUE(attached.has_value());
    EXPECT_EQ(attached->source_message_id, 50);

    store.clearUser(10);
    EXPECT_FALSE(store.find(10, asset->asset_id).has_value());
    EXPECT_FALSE(std::filesystem::exists(asset->local_path));
}

TEST(ImageAssetStoreIntegrationTest, RemovesOnlyAssetsAfterConversationBoundary)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    const auto databasePath = temporaryDirectory.path() + "/assets.db";
    const auto assetDirectory = temporaryDirectory.path() + "/images";

    ImageAssetStore store(databasePath, assetDirectory);
    const auto retained = store.saveBase64(10, "b2xk", "inbound", "", 10);
    const auto removed = store.saveBase64(10, "bmV3", "generated", "", 20);
    const auto otherUser = store.saveBase64(20, "b3RoZXI=", "generated", "", 30);
    ASSERT_TRUE(retained.has_value());
    ASSERT_TRUE(removed.has_value());
    ASSERT_TRUE(otherUser.has_value());

    store.removeByConversationFrom(10, 20);

    EXPECT_TRUE(store.find(10, retained->asset_id).has_value());
    EXPECT_FALSE(store.find(10, removed->asset_id).has_value());
    EXPECT_TRUE(store.find(20, otherUser->asset_id).has_value());
    EXPECT_TRUE(std::filesystem::exists(retained->local_path));
    EXPECT_FALSE(std::filesystem::exists(removed->local_path));
    EXPECT_TRUE(std::filesystem::exists(otherUser->local_path));
}
