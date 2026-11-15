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

// 生效方式只有两种（L2.e 起）：实时生效的是 Dynamic，其余是 Restart，不再产生 Rebuild
// （通信超时此前是过渡期的 Rebuild，L2.e 起归 Restart）
TEST(ConfigDiffTest, ClassifiesDynamicAndRestartChanges)
{
    SchemaConfig current;
    SchemaConfig candidate = current;
    candidate.bot.groupChatEnabled = false;
    candidate.communication.requestTimeoutMs = current.communication.requestTimeoutMs + 1000;
    candidate.chat.workerThreads = 8;

    const ConfigDiff diff = compareConfig(current, candidate);

    EXPECT_EQ(diff.size(), 3U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Dynamic), 1U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Rebuild), 0U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Restart), 2U);
    EXPECT_TRUE(containsChange(diff, "bot.group_chat_enabled",
                               ConfigChangeImpact::Dynamic));
    EXPECT_TRUE(containsChange(diff, "communication.defaults.request_timeout_ms",
                               ConfigChangeImpact::Restart));
    EXPECT_TRUE(containsChange(diff, "chat.worker_threads",
                               ConfigChangeImpact::Restart));
}

// 配置实时生效 L1.a：chat 组 6 个键接通应用，档位由 Rebuild 变为 Dynamic；
// worker_threads 只能重启（chat.top_p 已删除）
TEST(ConfigDiffTest, ChatKeysAreDynamicExceptWorkerThreads)
{
    SchemaConfig current;
    SchemaConfig candidate = current;
    candidate.chat.defaultModel = "next-model";
    candidate.chat.temperature = 0.5;
    candidate.chat.frequencyPenalty = 0.5;
    candidate.chat.presencePenalty = 0.5;
    candidate.chat.maxMessageTokens = 1024;
    candidate.chat.workerThreads = 4;
    candidate.chat.messageSurvivalSeconds = 60;

    const ConfigDiff diff = compareConfig(current, candidate);

    EXPECT_EQ(diff.size(), 7U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Dynamic), 6U);
    for (const char *path : {"chat.default_model", "chat.temperature", "chat.frequency_penalty",
                             "chat.presence_penalty", "chat.max_message_tokens",
                             "chat.message_survival_seconds"})
        EXPECT_TRUE(containsChange(diff, path, ConfigChangeImpact::Dynamic)) << path;
    EXPECT_TRUE(containsChange(diff, "chat.worker_threads", ConfigChangeImpact::Restart));
}

// 配置实时生效 L1.b：models 三个专用端点的 12 个字段接通应用，档位由 Rebuild 变为 Dynamic
TEST(ConfigDiffTest, ModelEndpointKeysAreDynamic)
{
    SchemaConfig current;
    SchemaConfig candidate = current;
    for (ModelEndpointSchema *endpoint :
         {&candidate.models.drawing, &candidate.models.vision, &candidate.models.worker})
    {
        endpoint->model = "next-model";
        endpoint->endpoint = "https://next.example.test";
        endpoint->apiKey = "sk-next";
        endpoint->apiStandard = "Anthropic";
    }

    const ConfigDiff diff = compareConfig(current, candidate);

    EXPECT_EQ(diff.size(), 12U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Dynamic), 12U);
    for (const char *endpoint : {"drawing", "vision", "worker"})
    {
        for (const char *field : {"model", "endpoint", "api_key", "api_standard"})
        {
            const std::string path = std::string("models.") + endpoint + "." + field;
            EXPECT_TRUE(containsChange(diff, path, ConfigChangeImpact::Dynamic)) << path;
        }
    }
}

// 配置实时生效 L1.c：voice 的 5 个键接通应用（开关与合成参数），档位变为 Dynamic
TEST(ConfigDiffTest, VoiceKeysAreDynamic)
{
    SchemaConfig current;
    SchemaConfig candidate = current;
    candidate.voice.enabled = !current.voice.enabled;
    candidate.voice.host = "127.0.0.2";
    candidate.voice.port = "9881";
    candidate.voice.referenceAudioPath = "next.wav";
    candidate.voice.referenceText = "next";

    const ConfigDiff diff = compareConfig(current, candidate);

    EXPECT_EQ(diff.size(), 5U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Dynamic), 5U);
    for (const char *path : {"voice.enabled", "voice.host", "voice.port", "voice.reference_audio_path",
                             "voice.reference_text"})
        EXPECT_TRUE(containsChange(diff, path, ConfigChangeImpact::Dynamic)) << path;
}

// 配置实时生效 L1.d：memory 除 enabled 外的 4 个键接通应用，档位变为 Dynamic；
// L2.b 起 enabled 也接通（后台线程常驻，开关只决定收不收新轮次）
TEST(ConfigDiffTest, MemoryKeysAreDynamic)
{
    SchemaConfig current;
    SchemaConfig candidate = current;
    candidate.memory.enabled = !current.memory.enabled;
    candidate.memory.model = "memory-next";
    candidate.memory.batchTurns = current.memory.batchTurns + 1;
    candidate.memory.idleMinutes = current.memory.idleMinutes + 1;
    candidate.memory.recallLimit = current.memory.recallLimit + 1;

    const ConfigDiff diff = compareConfig(current, candidate);

    EXPECT_EQ(diff.size(), 5U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Dynamic), 5U);
    for (const char *path : {"memory.enabled", "memory.model", "memory.batch_turns",
                             "memory.idle_minutes", "memory.recall_limit"})
        EXPECT_TRUE(containsChange(diff, path, ConfigChangeImpact::Dynamic)) << path;
}

// 配置实时生效 L1.e：web_search（除 enabled、provider）、web_fetch（除 enabled）与 network.proxy
// 接通应用，档位变为 Dynamic；L2.a 起两个 enabled 也接通；provider 只能重启（L2.e 起为 Restart）
TEST(ConfigDiffTest, WebSearchFetchAndProxyKeysAreDynamicExceptProvider)
{
    SchemaConfig current;
    SchemaConfig candidate = current;
    candidate.webSearch.enabled = !current.webSearch.enabled;
    candidate.webSearch.provider = "other";
    candidate.webSearch.endpoint = "https://search.example.test";
    candidate.webSearch.apiKey = "sk-next";
    candidate.webSearch.searchDepth = "advanced";
    candidate.webSearch.maxResults = current.webSearch.maxResults + 1;
    candidate.webSearch.maxContentChars = current.webSearch.maxContentChars + 1;
    candidate.webSearch.maxResponseBytes = current.webSearch.maxResponseBytes + 1;
    candidate.webSearch.connectTimeoutMs = current.webSearch.connectTimeoutMs + 1;
    candidate.webSearch.requestTimeoutMs = current.webSearch.requestTimeoutMs + 1;
    candidate.webFetch.enabled = !current.webFetch.enabled;
    candidate.webFetch.maxContentChars = current.webFetch.maxContentChars + 1;
    candidate.webFetch.maxResponseBytes = current.webFetch.maxResponseBytes + 1;
    candidate.webFetch.connectTimeoutMs = current.webFetch.connectTimeoutMs + 1;
    candidate.webFetch.requestTimeoutMs = current.webFetch.requestTimeoutMs + 1;
    candidate.webFetch.cacheTtlSeconds = current.webFetch.cacheTtlSeconds + 1;
    candidate.webFetch.cacheMaxEntries = current.webFetch.cacheMaxEntries + 1;
    candidate.proxy = "http://127.0.0.1:7890";

    const ConfigDiff diff = compareConfig(current, candidate);

    EXPECT_EQ(diff.size(), 18U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Dynamic), 17U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Restart), 1U);
    EXPECT_TRUE(containsChange(diff, "web_search.provider", ConfigChangeImpact::Restart));
    for (const char *path : {"web_search.enabled", "web_fetch.enabled"})
        EXPECT_TRUE(containsChange(diff, path, ConfigChangeImpact::Dynamic)) << path;
    EXPECT_TRUE(containsChange(diff, "network.proxy", ConfigChangeImpact::Dynamic));
    EXPECT_TRUE(containsChange(diff, "web_search.endpoint", ConfigChangeImpact::Dynamic));
    EXPECT_TRUE(containsChange(diff, "web_fetch.cache_max_entries", ConfigChangeImpact::Dynamic));
}

// L2.e：communication 的 17 个键只能重启生效（连接在启动时建立），档位 Restart
TEST(ConfigDiffTest, CommunicationKeysAreRestart)
{
    SchemaConfig current;
    SchemaConfig candidate = current;
    CommunicationSchema &next = candidate.communication;
    next.protocolType = "other";
    next.activeTransport = "other";
    TransportProfileSchema &profile = next.activeProfile;
    profile.type = "other";
    profile.host = "10.0.0.1";
    profile.port = static_cast<unsigned short>(current.communication.activeProfile.port + 1);
    profile.path = "/next";
    profile.accessToken = "token-next";
    profile.apiBaseUrl = "http://10.0.0.1:3000";
    profile.apiAccessToken = "api-token-next";
    profile.eventBindHost = "0.0.0.0";
    profile.eventBindPort = static_cast<unsigned short>(current.communication.activeProfile.eventBindPort + 1);
    profile.eventPath = "/events-next";
    profile.eventAccessToken = "event-token-next";
    profile.eventSecret = "secret-next";
    next.connectTimeoutMs = current.communication.connectTimeoutMs + 1;
    next.requestTimeoutMs = current.communication.requestTimeoutMs + 1;
    next.maxBodyBytes = current.communication.maxBodyBytes + 1;

    const ConfigDiff diff = compareConfig(current, candidate);

    EXPECT_EQ(diff.size(), 17U);
    EXPECT_EQ(diff.count(ConfigChangeImpact::Restart), 17U);
    for (const ConfigChange &change : diff.changes())
        EXPECT_EQ(change.path.rfind("communication.", 0), 0U) << change.path;
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
    // 配置实时生效 L2.c 起 affect.enabled 重载即生效
    EXPECT_TRUE(containsChange(diff, "affect.enabled", ConfigChangeImpact::Dynamic));
    // R1 三键缺省：基线、影子、跟随 worker
    EXPECT_EQ(loaded.config->affect.policy, "baseline");
    EXPECT_FALSE(loaded.config->affect.enforce);
    EXPECT_TRUE(loaded.config->affect.judgeModel.empty());
}

// R1 决策层三键：解析、非法 policy 回默认 + 诊断、无未知字段告警；policy、judge_model 为
// Restart，enforce 自配置实时生效 L1.f 起接通应用、为 Dynamic
TEST(ConfigLoaderAffectTest, ParsesDecisionLayerKeysAndClassifiesRestart)
{
    const std::string base = R"({
        "schema_version": 1,
        "bot": {"id": 10001, "manager_id": 10002, "name": "Klein"},
        "chat": {"default_model": "test-model"},
        "models": {"registry_path": "ModelsName.json"},
        "affect": AFFECT,
        "communication": {
            "protocol": {"type": "onebot"},
            "active_transport": "local",
            "transports": {
                "local": {"type": "reverse_websocket", "bind": "127.0.0.1", "port": 8600}
            }
        }
    })";
    const auto withAffect = [&base](const std::string &affect)
    {
        std::string text = base;
        text.replace(text.find("AFFECT"), 6, affect);
        return text;
    };

    TemporaryConfigFile file;
    file.write(withAffect(
        R"({"enabled": true, "policy": "judge", "enforce": true, "judge_model": "judge-model"})"));
    ConfigLoader loader;
    const ConfigLoadResult loaded = loader.loadFile(file.path.string());
    EXPECT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    EXPECT_EQ(loaded.config->affect.policy, "judge");
    EXPECT_TRUE(loaded.config->affect.enforce);
    EXPECT_EQ(loaded.config->affect.judgeModel, "judge-model");
    for (const auto &diagnostic : loaded.diagnostics)
        EXPECT_NE(diagnostic.path.rfind("affect", 0), 0u) << diagnostic.path << " " << diagnostic.message;

    SchemaConfig candidate = *loaded.config;
    candidate.affect.policy = "baseline";
    candidate.affect.enforce = false;
    candidate.affect.judgeModel = "other";
    const ConfigDiff diff = compareConfig(*loaded.config, candidate);
    // 配置实时生效：enforce 自 L1.f、policy 与 judge_model 自 L2.c 起重载即生效
    EXPECT_TRUE(containsChange(diff, "affect.policy", ConfigChangeImpact::Dynamic));
    EXPECT_TRUE(containsChange(diff, "affect.enforce", ConfigChangeImpact::Dynamic));
    EXPECT_TRUE(containsChange(diff, "affect.judge_model", ConfigChangeImpact::Dynamic));

    file.write(withAffect(R"({"enabled": true, "policy": "smart"})"));
    const ConfigLoadResult invalid = loader.loadFile(file.path.string());
    EXPECT_TRUE(invalid.canStart());
    ASSERT_NE(invalid.config, nullptr);
    EXPECT_EQ(invalid.config->affect.policy, "baseline");
    bool warned = false;
    for (const auto &diagnostic : invalid.diagnostics)
        if (diagnostic.path == "affect.policy" && diagnostic.severity == ConfigSeverity::Warning)
            warned = true;
    EXPECT_TRUE(warned);
}

// R1.1 群聊瞥视配置节：缺省默认值、解析、越界回默认 + 诊断；预算自配置实时生效 L1.f、
// enabled 自 L2.b 起为 Dynamic
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
    // 预算自配置实时生效 L1.f 起接通应用，enabled 自 L2.b 起接通
    EXPECT_TRUE(containsChange(diff, "glance.enabled", ConfigChangeImpact::Dynamic));
    EXPECT_TRUE(containsChange(diff, "glance.per_group_per_minute", ConfigChangeImpact::Dynamic));
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

// 能解析、但诊断里有 Error/Fatal 的候选同样不发布（2026-11-15 修复：此前 reload() 先把诊断
// 移进结果再判 canStart()，这类候选会被发布）。诊断要与加载器给出的逐条相同
namespace
{
void expectParsedCandidateRejected(const std::string &candidateText, ConfigSeverity severity,
                                   const std::string &diagnosticPath)
{
    ConfigLoader loader;
    const ConfigLoadResult initial = loader.loadText(validConfig);
    ASSERT_TRUE(initial.canStart());

    TemporaryConfigFile file;
    file.write(candidateText);
    const ConfigLoadResult expected = loader.loadFile(file.path.string());
    ASSERT_NE(expected.config, nullptr) << "候选必须能解析，才走得到这一步";
    ASSERT_FALSE(expected.canStart());

    ConfigSnapshotStore store(file.path.string(), initial.config);
    const std::shared_ptr<const ConfigSnapshot> before = store.current();
    const ConfigReloadResult reloaded = store.reload();

    EXPECT_FALSE(reloaded.success);
    EXPECT_EQ(reloaded.snapshot, before);
    EXPECT_EQ(store.current(), before);
    EXPECT_TRUE(reloaded.diff.empty());
    ASSERT_EQ(reloaded.diagnostics.size(), expected.diagnostics.size());
    for (std::size_t index = 0; index < expected.diagnostics.size(); ++index)
    {
        EXPECT_EQ(reloaded.diagnostics[index].severity, expected.diagnostics[index].severity);
        EXPECT_EQ(reloaded.diagnostics[index].category, expected.diagnostics[index].category);
        EXPECT_EQ(reloaded.diagnostics[index].path, expected.diagnostics[index].path);
        EXPECT_EQ(reloaded.diagnostics[index].message, expected.diagnostics[index].message);
    }
    bool found = false;
    for (const ConfigDiagnostic &diagnostic : reloaded.diagnostics)
    {
        if (diagnostic.severity == severity && diagnostic.path == diagnosticPath)
            found = true;
    }
    EXPECT_TRUE(found) << diagnosticPath;
}
}

TEST(ConfigSnapshotStoreTest, KeepsCurrentSnapshotWhenParsedCandidateHasFatal)
{
    nlohmann::json candidate = nlohmann::json::parse(validConfig);
    candidate["bot"]["group_chat_enabled"] = false; // 有变化：若被发布，差异不为空
    candidate["bot"].erase("id");                   // 必填项缺失 → Fatal
    expectParsedCandidateRejected(candidate.dump(), ConfigSeverity::Fatal, "bot.id");
}

TEST(ConfigSnapshotStoreTest, KeepsCurrentSnapshotWhenParsedCandidateHasError)
{
    nlohmann::json candidate = nlohmann::json::parse(validConfig);
    candidate["bot"]["group_chat_enabled"] = false;
    candidate["communication"]["transports"]["local"]["path"] = "onebot"; // 不以 / 开头 → Error
    expectParsedCandidateRejected(candidate.dump(), ConfigSeverity::Error,
                                  "communication.transports.local.path");
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

// bot.aliases：她也会答应的另一个名字（2026-10-30 引入；2026-11-08 用户定规改为单个字符串）。
// 缺省空、字符串原样、旧数组取第一项并告警、null 告警视为空、Restart 级
TEST(ConfigLoaderBotTest, AliasesDefaultToEmptyWhenMissing)
{
    ConfigLoader loader;
    const ConfigLoadResult loaded = loader.loadText(validConfig);
    ASSERT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    EXPECT_TRUE(loaded.config->bot.aliases.empty());
}

namespace
{
std::string aliasesConfig(const std::string &aliasesJson)
{
    return R"({
        "schema_version": 1,
        "bot": {"id": 10001, "name": "Klein", "aliases": )" + aliasesJson + R"(},
        "chat": {"default_model": "test-model"},
        "models": {"registry_path": "ModelsName.json"},
        "communication": {
            "protocol": {"type": "onebot"},
            "active_transport": "local",
            "transports": {
                "local": {"type": "reverse_websocket", "bind": "127.0.0.1", "port": 8600}
            }
        }
    })";
}

std::vector<ConfigDiagnostic> aliasDiagnostics(const ConfigLoadResult &loaded)
{
    std::vector<ConfigDiagnostic> result;
    for (const ConfigDiagnostic &diagnostic : loaded.diagnostics)
    {
        if (diagnostic.path == "bot.aliases")
            result.push_back(diagnostic);
    }
    return result;
}
}

TEST(ConfigLoaderBotTest, ParsesStringAliasesAndClassifiesRestart)
{
    ConfigLoader loader;
    const ConfigLoadResult loaded = loader.loadText(R"({
        "schema_version": 1,
        "bot": {"id": 10001, "manager_id": 10002, "name": "Klein",
                "aliases": "克莱茵"},
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
    EXPECT_EQ(loaded.config->bot.aliases, "克莱茵");
    EXPECT_TRUE(aliasDiagnostics(loaded).empty());

    SchemaConfig candidate = *loaded.config;
    candidate.bot.aliases = "Kc";
    const ConfigDiff diff = compareConfig(*loaded.config, candidate);
    EXPECT_EQ(diff.size(), 1U);
    // 配置实时生效 L2.d 起重载即生效
    EXPECT_TRUE(containsChange(diff, "bot.aliases", ConfigChangeImpact::Dynamic));
}

TEST(ConfigLoaderBotTest, EmptyStringAliasesMeansNoneWithoutDiagnostic)
{
    const ConfigLoadResult loaded = ConfigLoader().loadText(aliasesConfig(R"("")"));
    ASSERT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    EXPECT_TRUE(loaded.config->bot.aliases.empty());
    EXPECT_TRUE(aliasDiagnostics(loaded).empty());
}

// 旧的数组形态（2026-10-30 至 2026-11-08）：读取宽容，取第一个非空字符串元素，并发一条 Type 警告
TEST(ConfigLoaderBotTest, LegacyArrayAliasesTakesFirstItemWithTypeWarning)
{
    const ConfigLoadResult loaded =
        ConfigLoader().loadText(aliasesConfig(R"(["", 42, "克莱茵", "klein"])"));
    ASSERT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    EXPECT_EQ(loaded.config->bot.aliases, "克莱茵");
    const std::vector<ConfigDiagnostic> diagnostics = aliasDiagnostics(loaded);
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].severity, ConfigSeverity::Warning);
    EXPECT_EQ(diagnostics[0].category, ConfigErrorCategory::Type);

    const ConfigLoadResult empty = ConfigLoader().loadText(aliasesConfig("[]"));
    ASSERT_TRUE(empty.canStart());
    EXPECT_TRUE(empty.config->bot.aliases.empty());
    EXPECT_EQ(aliasDiagnostics(empty).size(), 1U);
}

TEST(ConfigLoaderBotTest, NullOrNonStringAliasesFallBackToEmptyWithTypeWarning)
{
    for (const char *value : {"null", "42", "{}"})
    {
        const ConfigLoadResult loaded = ConfigLoader().loadText(aliasesConfig(value));
        ASSERT_TRUE(loaded.canStart()) << value;
        ASSERT_NE(loaded.config, nullptr);
        EXPECT_TRUE(loaded.config->bot.aliases.empty()) << value;
        const std::vector<ConfigDiagnostic> diagnostics = aliasDiagnostics(loaded);
        ASSERT_EQ(diagnostics.size(), 1U) << value;
        EXPECT_EQ(diagnostics[0].severity, ConfigSeverity::Warning) << value;
        EXPECT_EQ(diagnostics[0].category, ConfigErrorCategory::Type) << value;
    }
}
