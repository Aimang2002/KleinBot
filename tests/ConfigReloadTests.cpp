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
    "resources": {
        "personality_directory": "source/personality/",
        "help_file": "source/help.txt"
    },
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
    candidate.accessibilityChat = true;
    candidate.voice.host = "http://127.0.0.1";
    candidate.chat.workerThreads = 8;

    const ConfigDiff diff = compareConfig(current, candidate);

    EXPECT_EQ(diff.size(), 3U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Dynamic), 1U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Rebuild), 1U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Restart), 1U);
    EXPECT_TRUE(containsChange(diff, "features.accessibility_chat",
                               ConfigChangeImpact::Dynamic));
    EXPECT_TRUE(containsChange(diff, "voice.host", ConfigChangeImpact::Rebuild));
    EXPECT_TRUE(containsChange(diff, "chat.worker_threads",
                               ConfigChangeImpact::Restart));
}

TEST(ConfigSnapshotStoreTest, PublishesValidatedCandidateWithoutApplyingRuntimeObjects)
{
    ConfigLoader loader;
    const ConfigLoadResult initial = loader.loadText(validConfig);
    ASSERT_TRUE(initial.canStart());

    TemporaryConfigFile file;
    nlohmann::json candidate = nlohmann::json::parse(validConfig);
    candidate["features"] = {{"accessibility_chat", true}};
    candidate["storage"] = {{"conversation_database", "next.db"}};
    file.write(candidate.dump());

    ConfigSnapshotStore store(file.path.string(), initial.config);
    const std::shared_ptr<const ConfigSnapshot> startup = store.current();
    const ConfigReloadResult reloaded = store.reload();

    ASSERT_TRUE(reloaded.success);
    EXPECT_TRUE(reloaded.snapshot->schema->accessibilityChat);
    EXPECT_EQ(reloaded.snapshot->runtime.storage.conversationDatabase, "next.db");
    EXPECT_FALSE(startup->schema->accessibilityChat);
    EXPECT_NE(startup, reloaded.snapshot);
    EXPECT_TRUE(containsChange(reloaded.diff, "features.accessibility_chat",
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
    "resources": {
        "personality_directory": "source/personality/",
        "help_file": "source/help.txt"
    },
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
