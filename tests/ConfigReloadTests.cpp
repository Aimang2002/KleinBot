#include <gtest/gtest.h>

#include "Bootstrap/ConfigDiff.h"
#include "Bootstrap/ConfigSnapshotStore.h"
#include "Configuration/ConfigLoader.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

TEST(ConfigLoaderAffectTest, ParsesEnabledFlagAndClassifiesRestart)
{
    TemporaryConfigFile file;
    file.write(R"({
        "schema_version": 1,
        "bot": {"id": 10001, "manager_id": 10002, "name": "Klein"},
        "chat": {"default_model": "test-model"},
        "models": {"registry_path": "ModelsName.json"},
        "affect": {"enabled": true},
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
    })");

    ConfigLoader loader;
    const ConfigLoadResult loaded = loader.loadFile(file.path.string());
    EXPECT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    EXPECT_TRUE(loaded.config->affect.enabled);

    SchemaConfig candidate = *loaded.config;
    candidate.affect.enabled = false;
    const ConfigDiff diff = compareConfig(*loaded.config, candidate);
    EXPECT_TRUE(containsChange(diff, "affect.enabled", ConfigChangeImpact::Restart));
}

// R1.1 群聊瞥视配置节：缺省默认值、解析、越界回默认 + 诊断、Restart 级
TEST(ConfigLoaderGlanceTest, DefaultsWhenSectionMissing)
{
    ConfigLoader loader;
    const ConfigLoadResult loaded = loader.loadText(validConfig);
    ASSERT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    EXPECT_FALSE(loaded.config->glance.enabled);
    EXPECT_EQ(loaded.config->glance.perGroupPerMinute, 6);
}

TEST(ConfigLoaderGlanceTest, ParsesSectionAndClassifiesRestart)
{
    ConfigLoader loader;
    const ConfigLoadResult loaded = loader.loadText(R"({
        "schema_version": 1,
        "bot": {"id": 10001, "manager_id": 10002, "name": "Klein"},
        "chat": {"default_model": "test-model"},
        "models": {"registry_path": "ModelsName.json"},
        "glance": {"enabled": true, "per_group_per_minute": 12},
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
    })");
    ASSERT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    EXPECT_TRUE(loaded.config->glance.enabled);
    EXPECT_EQ(loaded.config->glance.perGroupPerMinute, 12);
    for (const ConfigDiagnostic &diagnostic : loaded.diagnostics)
        EXPECT_EQ(diagnostic.path.rfind("glance", 0), std::string::npos)
            << "合法 glance 节不应产生诊断：" << diagnostic.path << " " << diagnostic.message;

    SchemaConfig candidate = *loaded.config;
    candidate.glance.enabled = false;
    candidate.glance.perGroupPerMinute = 3;
    const ConfigDiff diff = compareConfig(*loaded.config, candidate);
    EXPECT_EQ(diff.size(), 2U);
    EXPECT_TRUE(containsChange(diff, "glance.enabled", ConfigChangeImpact::Restart));
    EXPECT_TRUE(containsChange(diff, "glance.per_group_per_minute", ConfigChangeImpact::Restart));
}

TEST(ConfigLoaderGlanceTest, OutOfRangeBudgetFallsBackToDefaultWithDiagnostic)
{
    for (const char *value : {"0", "61", "-3"})
    {
        ConfigLoader loader;
        const std::string text = std::string(R"({
            "schema_version": 1,
            "bot": {"id": 10001, "manager_id": 10002, "name": "Klein"},
            "chat": {"default_model": "test-model"},
            "models": {"registry_path": "ModelsName.json"},
            "glance": {"enabled": true, "per_group_per_minute": )") + value + R"(, "extra": 1},
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
        const ConfigLoadResult loaded = loader.loadText(text);
        ASSERT_TRUE(loaded.canStart()) << value;
        ASSERT_NE(loaded.config, nullptr);
        EXPECT_TRUE(loaded.config->glance.enabled);
        EXPECT_EQ(loaded.config->glance.perGroupPerMinute, 6) << value;
        bool rangeDiagnostic = false;
        bool unknownDiagnostic = false;
        for (const ConfigDiagnostic &diagnostic : loaded.diagnostics)
        {
            if (diagnostic.path == "glance.per_group_per_minute" &&
                diagnostic.category == ConfigErrorCategory::Range)
                rangeDiagnostic = true;
            if (diagnostic.category == ConfigErrorCategory::Unknown &&
                diagnostic.path.find("glance") != std::string::npos &&
                diagnostic.path.find("extra") != std::string::npos)
                unknownDiagnostic = true;
        }
        EXPECT_TRUE(rangeDiagnostic) << value;
        EXPECT_TRUE(unknownDiagnostic) << "节内白名单生效";
    }
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

TEST(PushSchemaTest, SectionMissingKeepsDefaultsDisabled)
{
    ConfigLoader loader;
    const ConfigLoadResult result = loader.loadText(validConfig);

    ASSERT_TRUE(result.canStart());
    ASSERT_NE(result.config, nullptr);
    EXPECT_FALSE(result.config->push.enabled);
    EXPECT_EQ(result.config->push.bind, "127.0.0.1");
    EXPECT_EQ(result.config->push.port, kDefaultPushPort);
    EXPECT_EQ(result.config->push.maxPerHour, 60);
    EXPECT_TRUE(result.config->push.allowedGroups.empty());
    EXPECT_TRUE(result.config->push.allowedUsers.empty());
}

TEST(PushSchemaTest, MissingTokenOrEmptyWhitelistDisablesPushWithoutBlockingStartup)
{
    // 与 webui 同语义：令牌缺失或白名单为空时降级关闭推送门，不阻断启动
    const char *missingToken = R"({
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
    "push": {
        "enabled": true,
        "access_token": {"from_env": "KLEIN_ABSENT_PUSH_TOKEN_FOR_TEST"},
        "allowed_groups": [123456]
    }
})";
    const char *emptyWhitelist = R"({
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
    "push": {
        "enabled": true,
        "access_token": {"literal": "push-token"},
        "allowed_groups": [],
        "allowed_users": []
    }
})";

    ConfigLoader loader;
    for (const char *config : {missingToken, emptyWhitelist})
    {
        const ConfigLoadResult result = loader.loadText(config);
        EXPECT_TRUE(result.canStart()) << config;
        ASSERT_NE(result.config, nullptr) << config;
        EXPECT_FALSE(result.config->push.enabled) << config;
        for (const ConfigDiagnostic &diagnostic : result.diagnostics)
        {
            EXPECT_NE(diagnostic.severity, ConfigSeverity::Error) << config;
            EXPECT_NE(diagnostic.severity, ConfigSeverity::Fatal) << config;
        }
    }
}

TEST(PushSchemaTest, LoadsWhitelistAndSkipsInvalidElements)
{
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
    "push": {
        "enabled": true,
        "access_token": {"literal": "push-token"},
        "allowed_groups": [123456, "bad", 0],
        "allowed_users": [654321],
        "max_per_hour": 120
    }
})";

    ConfigLoader loader;
    const ConfigLoadResult result = loader.loadText(config);

    EXPECT_TRUE(result.canStart());
    ASSERT_NE(result.config, nullptr);
    EXPECT_TRUE(result.config->push.enabled);
    EXPECT_EQ(result.config->push.accessToken, "push-token");
    ASSERT_EQ(result.config->push.allowedGroups.size(), 1U);
    EXPECT_EQ(result.config->push.allowedGroups[0], 123456);
    ASSERT_EQ(result.config->push.allowedUsers.size(), 1U);
    EXPECT_EQ(result.config->push.allowedUsers[0], 654321);
    EXPECT_EQ(result.config->push.maxPerHour, 120);
}

TEST(PushConfigDiffTest, ClassifiesAllPushKeysAsRestart)
{
    SchemaConfig current;
    SchemaConfig candidate = current;
    candidate.push.enabled = true;
    candidate.push.bind = "0.0.0.0";
    candidate.push.port = 60000;
    candidate.push.accessToken = "next";
    candidate.push.allowedGroups = {111111};
    candidate.push.allowedUsers = {222222};
    candidate.push.maxPerHour = 30;

    const ConfigDiff diff = compareConfig(current, candidate);

    EXPECT_EQ(diff.size(), 7U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Restart), 7U);
    for (const char *path : {"push.enabled", "push.bind", "push.port", "push.access_token",
                             "push.allowed_groups", "push.allowed_users", "push.max_per_hour"})
        EXPECT_TRUE(containsChange(diff, path, ConfigChangeImpact::Restart)) << path;
}

// bot.aliases（2026-10-30）：她也会答应的名字；缺省空、空串告警丢弃、去重、Restart 级
TEST(ConfigLoaderBotTest, AliasesDefaultToEmptyWhenMissing)
{
    ConfigLoader loader;
    const ConfigLoadResult loaded = loader.loadText(validConfig);
    ASSERT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    EXPECT_TRUE(loaded.config->bot.aliases.empty());
}

TEST(ConfigLoaderBotTest, ParsesAliasesDropsEmptyAndDuplicatesAndClassifiesRestart)
{
    ConfigLoader loader;
    const ConfigLoadResult loaded = loader.loadText(R"({
        "schema_version": 1,
        "bot": {"id": 10001, "manager_id": 10002, "name": "Klein",
                "aliases": ["克莱茵", "", "klein", "克莱茵", 42]},
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
    })");
    ASSERT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    const std::vector<std::string> expected{"克莱茵", "klein"};
    EXPECT_EQ(loaded.config->bot.aliases, expected);

    std::size_t aliasWarnings = 0;
    for (const ConfigDiagnostic &diagnostic : loaded.diagnostics)
    {
        if (diagnostic.path == "bot.aliases")
        {
            EXPECT_EQ(diagnostic.severity, ConfigSeverity::Warning) << diagnostic.message;
            ++aliasWarnings;
        }
    }
    EXPECT_EQ(aliasWarnings, 2U) << "空串与非字符串元素各一条告警，重复项静默去重";

    SchemaConfig candidate = *loaded.config;
    candidate.bot.aliases = {"克莱茵"};
    const ConfigDiff diff = compareConfig(*loaded.config, candidate);
    EXPECT_EQ(diff.size(), 1U);
    EXPECT_TRUE(containsChange(diff, "bot.aliases", ConfigChangeImpact::Restart));
}

TEST(ConfigLoaderBotTest, NonArrayAliasesFallBackToEmptyWithDiagnostic)
{
    ConfigLoader loader;
    const ConfigLoadResult loaded = loader.loadText(R"({
        "schema_version": 1,
        "bot": {"id": 10001, "name": "Klein", "aliases": "克莱茵"},
        "chat": {"default_model": "test-model"},
        "models": {"registry_path": "ModelsName.json"},
        "communication": {
            "protocol": {"type": "onebot"},
            "active_transport": "local",
            "transports": {
                "local": {"type": "reverse_websocket", "bind": "127.0.0.1", "port": 8600}
            }
        }
    })");
    ASSERT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    EXPECT_TRUE(loaded.config->bot.aliases.empty());
    bool found = false;
    for (const ConfigDiagnostic &diagnostic : loaded.diagnostics)
    {
        if (diagnostic.path == "bot.aliases" && diagnostic.severity == ConfigSeverity::Warning)
            found = true;
    }
    EXPECT_TRUE(found);
}
