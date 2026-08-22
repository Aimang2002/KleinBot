#include <gtest/gtest.h>

#include "Asset/ImageAssetStore.h"
#include "Command/ResetChatCommand.h"
#include "Command/ResetContextCommand.h"
#include "Persistence/ConversationStore.h"
#include "Tool/SendImageTool.h"
#include "UserSession/UserSessionService.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sqlite3.h>
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

namespace
{
std::string writeModelRegistryFile(const std::string &directory)
{
    const std::filesystem::path path =
        std::filesystem::path(directory) / "models.json";
    {
        std::ofstream output(path);
        output << R"({
            "Models": [
                {
                    "ModelName": ["test-model"],
                    "api_key": "key",
                    "api_endpoint": "https://example.test/chat",
                    "APIStandard": "OpenAI"
                }
            ]
        })";
    }
    return path.string();
}
}

TEST(UserSessionWindowTest, KeepsHistoryHeadStableUntilHighWatermark)
{
    TemporaryDirectory temporaryDirectory;
    ConversationStore store(temporaryDirectory.path() + "/conversation.db");
    ModelRegistry registry(writeModelRegistryFile(temporaryDirectory.path()));
    ChatOptions options;
    options.defaultModel = "test-model";
    BotIdentity bot;
    UserSessionService session(registry, store, bot, options);

    for (int i = 1; i <= 40; ++i)
        session.appendMessage(10, i % 2 == 1 ? "user" : "assistant",
                              "消息" + std::to_string(i));

    auto bundle = session.buildChatRequest(10);
    ASSERT_TRUE(bundle.has_value());
    ASSERT_EQ(bundle->request.history.size(), 40U);
    // 超过旧上限 20 条但未到高水位 40 时头部不动，前缀保持稳定
    EXPECT_EQ(bundle->request.history.front().content, "消息1");
    EXPECT_EQ(bundle->request.history.back().content, "消息40");

    session.appendMessage(10, "user", "消息41");
    session.appendMessage(10, "assistant", "消息42");
    bundle = session.buildChatRequest(10);
    ASSERT_TRUE(bundle.has_value());
    // 越过高水位后一次性截回低水位 20 条，之后头部重新稳定
    ASSERT_EQ(bundle->request.history.size(), 20U);
    EXPECT_EQ(bundle->request.history.front().content, "消息23");
    EXPECT_EQ(bundle->request.history.back().content, "消息42");

    // 关键回归：截断之后的轮次头部不得继续滑动，否则退化为滑窗，
    // 每轮请求的前缀缓存都会整体作废
    for (int i = 43; i <= 62; ++i)
        session.appendMessage(10, i % 2 == 1 ? "user" : "assistant",
                              "消息" + std::to_string(i));
    bundle = session.buildChatRequest(10);
    ASSERT_TRUE(bundle.has_value());
    ASSERT_EQ(bundle->request.history.size(), 40U);
    EXPECT_EQ(bundle->request.history.front().content, "消息23");
    EXPECT_EQ(bundle->request.history.back().content, "消息62");

    // 第二次越界：再次一次性截回低水位，进入下一个稳定周期
    session.appendMessage(10, "user", "消息63");
    session.appendMessage(10, "assistant", "消息64");
    bundle = session.buildChatRequest(10);
    ASSERT_TRUE(bundle.has_value());
    ASSERT_EQ(bundle->request.history.size(), 20U);
    EXPECT_EQ(bundle->request.history.front().content, "消息45");
    EXPECT_EQ(bundle->request.history.back().content, "消息64");
}

TEST(UserSessionWindowTest, DropsExpiredMessagesOnColdStartLoad)
{
    TemporaryDirectory temporaryDirectory;
    ConversationStore store(temporaryDirectory.path() + "/conversation.db");
    ModelRegistry registry(writeModelRegistryFile(temporaryDirectory.path()));
    ChatOptions options;
    options.defaultModel = "test-model";
    options.messageSurvivalSeconds = 3600;
    BotIdentity bot;

    const time_t now = std::time(nullptr);
    store.append(10, "user", "过期消息", now - 7200);
    store.append(10, "assistant", "存活消息", now);

    UserSessionService session(registry, store, bot, options);
    auto bundle = session.buildChatRequest(10);
    ASSERT_TRUE(bundle.has_value());
    EXPECT_EQ(bundle->request.history.size(), 1U);
    EXPECT_EQ(bundle->request.history.front().content, "存活消息");
}

TEST(ConversationStoreIntegrationTest, ContextStartBoundaryFiltersColdStartLoadOnly)
{
    TemporaryDirectory temporaryDirectory;
    ConversationStore store(temporaryDirectory.path() + "/conversation.db");
    store.append(10, "user", "旧话题消息", 1000);
    const int64_t boundary = store.append(10, "assistant", "旧话题回答", 1001);
    store.append(20, "user", "其他用户", 1002);
    ASSERT_GT(boundary, 0);

    EXPECT_EQ(store.contextStartId(10), 0);
    store.setContextStartId(10, boundary + 1);
    EXPECT_EQ(store.contextStartId(10), boundary + 1);
    EXPECT_EQ(store.contextStartId(20), 0);

    // 起点只影响冷启动加载；召回用的全量读取和检索不受影响
    EXPECT_TRUE(store.loadFrom(10, store.contextStartId(10)).empty());
    ASSERT_EQ(store.loadAll(10).size(), 2U);
    EXPECT_EQ(store.search(10, "旧话题").size(), 2U);

    store.append(10, "user", "新话题消息", 1003);
    const auto active = store.loadFrom(10, store.contextStartId(10));
    ASSERT_EQ(active.size(), 1U);
    EXPECT_EQ(active.front().content, "新话题消息");

    store.setContextStartId(10, 0);
    EXPECT_EQ(store.loadFrom(10, 0).size(), 3U);
}

TEST(UserSessionWindowTest, LightResetClearsWindowKeepsHistoryAndPersistsAcrossRestart)
{
    TemporaryDirectory temporaryDirectory;
    ConversationStore store(temporaryDirectory.path() + "/conversation.db");
    ModelRegistry registry(writeModelRegistryFile(temporaryDirectory.path()));
    ChatOptions options;
    options.defaultModel = "test-model";
    BotIdentity bot;

    {
        UserSessionService session(registry, store, bot, options);
        session.appendMessage(10, "user", "旧话题提问");
        session.appendMessage(10, "assistant", "旧话题回答");
        session.resetChat(10);

        // 窗口立即清空，模型从新话题开始
        auto bundle = session.buildChatRequest(10);
        ASSERT_TRUE(bundle.has_value());
        EXPECT_TRUE(bundle->request.history.empty());

        // SQLite 原始历史保留，召回链路仍能命中旧话题
        ASSERT_EQ(store.loadAll(10).size(), 2U);
        EXPECT_EQ(store.search(10, "旧话题").size(), 2U);

        session.appendMessage(10, "user", "新话题消息");
        bundle = session.buildChatRequest(10);
        ASSERT_TRUE(bundle.has_value());
        ASSERT_EQ(bundle->request.history.size(), 1U);
        EXPECT_EQ(bundle->request.history.front().content, "新话题消息");

        // 轻重置后删除上条只作用于边界之后的轮次，不回删旧话题
        EXPECT_EQ(session.removePreviousContext(10), "上条对话已被删除！");
        ASSERT_EQ(store.loadAll(10).size(), 2U);
        EXPECT_EQ(session.removePreviousContext(10), "没有上下文！");
        ASSERT_EQ(store.loadAll(10).size(), 2U);
    }

    // 模拟重启：新实例冷启动只加载起点之后的消息，旧话题不会复活
    UserSessionService restarted(registry, store, bot, options);
    auto bundle = restarted.buildChatRequest(10);
    ASSERT_TRUE(bundle.has_value());
    EXPECT_TRUE(bundle->request.history.empty());
    ASSERT_EQ(store.loadAll(10).size(), 2U);
}

TEST(UserSessionWindowTest, HeavyResetClearsBoundaryAndAllHistory)
{
    TemporaryDirectory temporaryDirectory;
    ConversationStore store(temporaryDirectory.path() + "/conversation.db");
    ModelRegistry registry(writeModelRegistryFile(temporaryDirectory.path()));
    ChatOptions options;
    options.defaultModel = "test-model";
    BotIdentity bot;

    UserSessionService session(registry, store, bot, options);
    session.appendMessage(10, "user", "第一轮");
    session.resetChat(10);
    session.appendMessage(10, "user", "第二轮");
    session.resetContext(10);

    EXPECT_TRUE(store.loadAll(10).empty());
    EXPECT_EQ(store.contextStartId(10), 0);

    UserSessionService restarted(registry, store, bot, options);
    auto bundle = restarted.buildChatRequest(10);
    ASSERT_TRUE(bundle.has_value());
    EXPECT_TRUE(bundle->request.history.empty());
}

TEST(ResetCommandsIntegrationTest, MapsTriggersToLightAndHeavyResets)
{
    TemporaryDirectory temporaryDirectory;
    ConversationStore store(temporaryDirectory.path() + "/conversation.db");
    ModelRegistry registry(writeModelRegistryFile(temporaryDirectory.path()));
    ChatOptions options;
    options.defaultModel = "test-model";
    BotIdentity bot;
    UserSessionService session(registry, store, bot, options);

    ResetChatCommand light(session);
    ResetContextCommand heavy(session);
    EXPECT_TRUE(light.canHandle("#重置对话"));
    EXPECT_FALSE(light.canHandle("#重置上下文"));
    EXPECT_TRUE(heavy.canHandle("#重置上下文"));
    EXPECT_FALSE(heavy.canHandle("#重置对话"));

    InboundMessage data;
    const CommandContext context{10, 0, "private", data};
    session.appendMessage(10, "user", "内容");

    const auto lightReply = std::get<TextMessage>(light.execute(context).payload).content;
    EXPECT_EQ(lightReply, "对话重置成功。");
    EXPECT_EQ(store.loadAll(10).size(), 1U);

    const auto heavyReply = std::get<TextMessage>(heavy.execute(context).payload).content;
    EXPECT_NE(heavyReply.find("彻底"), std::string::npos);
    EXPECT_TRUE(store.loadAll(10).empty());
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
    EXPECT_TRUE(std::regex_match(asset->asset_id, std::regex(R"(^img_[0-9a-f]{32}$)")));
    EXPECT_EQ(asset->asset_id.find("_10_"), std::string::npos);
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

TEST(SendImageToolIntegrationTest, SendsLatestImageAsTerminalResultWithoutExposingAssetId)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    ImageAssetStore store(temporaryDirectory.path() + "/assets.db",
                          temporaryDirectory.path() + "/images");
    const auto firstAsset = store.saveBase64(10, "Zmlyc3Q=", "generated", "first", 30);
    const auto latestAsset = store.saveBase64(10, "bGF0ZXN0", "generated", "latest", 31);
    ASSERT_TRUE(firstAsset.has_value());
    ASSERT_TRUE(latestAsset.has_value());
    EXPECT_NE(firstAsset->asset_id, latestAsset->asset_id);

    sqlite3 *database = nullptr;
    ASSERT_EQ(sqlite3_open((temporaryDirectory.path() + "/assets.db").c_str(), &database), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(database,
                           "UPDATE image_assets SET created_at=1000 WHERE user_id=10;",
                           nullptr, nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(database);

    SendImageTool tool(store);
    const auto result = tool.execute(R"({"source":"generated"})", {10, 31});

    EXPECT_TRUE(result.terminal);
    EXPECT_TRUE(result.suppress_text_reply);
    EXPECT_EQ(result.model_content, "图片已发送。");
    EXPECT_EQ(result.model_content.find(latestAsset->asset_id), std::string::npos);
    ASSERT_EQ(result.outbound_messages.size(), 1U);
    const auto &image = std::get<ImageMessage>(result.outbound_messages.front());
    EXPECT_EQ(image.source, ImageMessage::Source::LocalPath);
    EXPECT_EQ(image.data, latestAsset->local_path);
}
