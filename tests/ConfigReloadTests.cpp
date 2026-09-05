#include <gtest/gtest.h>

#include "Bootstrap/ConfigDiff.h"
#include "Bootstrap/ConfigSnapshotStore.h"
#include "Configuration/ConfigLoader.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const char *validConfig = R"({
    "schema_version": 1,
    "bot": {"id": 10001, "manager_id": 10002, "name": "Klein"},
    "chat": {"default_model": "test-model"},
    "models": {"registry_path": "ModelsName.json"},
    "communication": {
        "protocol": {"type": "onebot"},
        "active_transport": "local",
        "transports": {
            "local": {
                "type": "reverse_websocket",
                "bind": "127.0.0.1",
                "port": 8600
            }
        }
    }
})";

class TemporaryConfigFile
{
public:
    TemporaryConfigFile()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("kleinbot-config-reload-" + std::to_string(suffix) + ".json");
    }

    ~TemporaryConfigFile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    void write(const std::string &content) const
    {
        std::ofstream output(path);
        output << content;
    }

    std::filesystem::path path;
};

bool containsChange(const ConfigDiff &diff, const std::string &path,
                    ConfigChangeImpact impact)
{
    for (const ConfigChange &change : diff.changes())
    {
        if (change.path == path && change.impact == impact)
            return true;
    }
    return false;
}
}

TEST(ConfigDiffTest, ClassifiesDynamicRebuildAndRestartChanges)
{
    SchemaConfig current;
    SchemaConfig candidate = current;
    candidate.bot.groupChatEnabled = false;
    candidate.voice.host = "http://127.0.0.1";
    candidate.chat.workerThreads = 8;

    const ConfigDiff diff = compareConfig(current, candidate);

    EXPECT_EQ(diff.size(), 3U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Dynamic), 1U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Rebuild), 1U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Restart), 1U);
    EXPECT_TRUE(containsChange(diff, "bot.group_chat_enabled",
                               ConfigChangeImpact::Dynamic));
    EXPECT_TRUE(containsChange(diff, "voice.host", ConfigChangeImpact::Rebuild));
    EXPECT_TRUE(containsChange(diff, "chat.worker_threads",
                               ConfigChangeImpact::Restart));
}

TEST(PersonaSchemaTest, QuoteReplyDefaultsOnAndMapsIntoMessageOptions)
{
    ConfigLoader loader;

    // 缺省：不写 persona 节 → quoteReply 默认 true
    const ConfigLoadResult withoutNode = loader.loadText(validConfig);
    ASSERT_TRUE(withoutNode.canStart());
    EXPECT_TRUE(withoutNode.config->persona.humanize.quoteReply);
    const RuntimeSettings defaultRuntime = buildRuntimeSettings(*withoutNode.config);
    EXPECT_TRUE(defaultRuntime.message.humanizeQuoteReply);

    // 显式关闭 + reload diff 登记
    nlohmann::json candidate = nlohmann::json::parse(validConfig);
    candidate["persona"] = {{"humanize", {{"quote_reply", false}}}};
    const ConfigLoadResult disabled = loader.loadText(candidate.dump());
    ASSERT_TRUE(disabled.canStart());
    EXPECT_FALSE(disabled.config->persona.humanize.quoteReply);
    EXPECT_FALSE(buildRuntimeSettings(*disabled.config).message.humanizeQuoteReply);

    const ConfigDiff diff = compareConfig(*withoutNode.config, *disabled.config);
    EXPECT_TRUE(containsChange(diff, "persona.humanize.quote_reply",
                               ConfigChangeImpact::Rebuild));

    // 未知字段警告但不致命
    candidate["persona"]["humanize"]["unknown_switch"] = 1;
    const ConfigLoadResult withUnknown = loader.loadText(candidate.dump());
    EXPECT_TRUE(withUnknown.canStart());
}

TEST(ConfigSnapshotStoreTest, PublishesValidatedCandidateWithoutApplyingRuntimeObjects)
{
    ConfigLoader loader;
    const ConfigLoadResult initial = loader.loadText(validConfig);
    ASSERT_TRUE(initial.canStart());

    TemporaryConfigFile file;
    nlohmann::json candidate = nlohmann::json::parse(validConfig);
    candidate["bot"]["group_chat_enabled"] = false;
    candidate["storage"] = {{"conversation_database", "next.db"}};
    file.write(candidate.dump());

    ConfigSnapshotStore store(file.path.string(), initial.config);
    const std::shared_ptr<const ConfigSnapshot> startup = store.current();
    const ConfigReloadResult reloaded = store.reload();

    ASSERT_TRUE(reloaded.success);
    EXPECT_FALSE(reloaded.snapshot->schema->bot.groupChatEnabled);
    EXPECT_EQ(reloaded.snapshot->runtime.storage.conversationDatabase, "next.db");
    EXPECT_TRUE(startup->schema->bot.groupChatEnabled);
    EXPECT_NE(startup, reloaded.snapshot);
    EXPECT_TRUE(containsChange(reloaded.diff, "bot.group_chat_enabled",
                               ConfigChangeImpact::Dynamic));
    EXPECT_TRUE(containsChange(reloaded.diff, "storage.conversation_database",
                               ConfigChangeImpact::Restart));
}

TEST(ConfigSnapshotStoreTest, KeepsCurrentSnapshotWhenCandidateIsInvalid)
{
    ConfigLoader loader;
    const ConfigLoadResult initial = loader.loadText(validConfig);
    ASSERT_TRUE(initial.canStart());

    TemporaryConfigFile file;
    file.write("{");
    ConfigSnapshotStore store(file.path.string(), initial.config);
    const std::shared_ptr<const ConfigSnapshot> before = store.current();

    const ConfigReloadResult reloaded = store.reload();

    EXPECT_FALSE(reloaded.success);
    EXPECT_EQ(reloaded.snapshot, before);
    EXPECT_EQ(store.current(), before);
    EXPECT_TRUE(reloaded.diff.empty());
    EXPECT_FALSE(reloaded.diagnostics.empty());
}

TEST(WebUiSchemaTest, MissingTokenEnvironmentDisablesPanelWithoutBlockingStartup)
{
    // 面板令牌引用不存在的环境变量时应降级关闭面板，而不是以 Error 阻断启动
    const char *config = R"({
    "schema_version": 1,
    "bot": {"id": 10001},
    "chat": {"default_model": "test-model"},
    "models": {"registry_path": "ModelsName.json"},
    "communication": {
        "protocol": {"type": "onebot"},
        "active_transport": "local",
        "transports": {
            "local": {"type": "reverse_websocket", "bind": "127.0.0.1", "port": 8600}
        }
    },
    "webui": {
        "enabled": true,
        "access_token": {"from_env": "KLEIN_ABSENT_WEBUI_TOKEN_FOR_TEST"}
    }
})";

    ConfigLoader loader;
    const ConfigLoadResult result = loader.loadText(config);

    EXPECT_TRUE(result.canStart());
    ASSERT_NE(result.config, nullptr);
    EXPECT_FALSE(result.config->webUi.enabled);
    for (const ConfigDiagnostic &diagnostic : result.diagnostics)
    {
        EXPECT_NE(diagnostic.severity, ConfigSeverity::Error);
        EXPECT_NE(diagnostic.severity, ConfigSeverity::Fatal);
    }
}
