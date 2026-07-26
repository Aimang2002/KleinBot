#include <gtest/gtest.h>

#include "Bootstrap/RuntimeSettings.h"
#include "Configuration/ConfigLoader.h"

#include <filesystem>
#include <fstream>

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

bool hasDiagnostic(const ConfigLoadResult &result, ConfigSeverity severity, const std::string &path)
{
    for (const auto &diagnostic : result.diagnostics)
    {
        if (diagnostic.severity == severity && diagnostic.path == path)
            return true;
    }
    return false;
}
}

TEST(ConfigLoaderTest, DecodesTypedConfigurationAndDefaults)
{
    ConfigLoader loader;
    const ConfigLoadResult result = loader.loadText(validConfig);

    ASSERT_TRUE(result.canStart());
    ASSERT_NE(result.config, nullptr);
    EXPECT_EQ(result.config->bot.id, 10001U);
    EXPECT_EQ(result.config->chat.workerThreads, 4U);
    const RuntimeSettings runtime = buildRuntimeSettings(*result.config);
    EXPECT_EQ(runtime.transport.mode, TransportMode::ReverseWebSocket);
    EXPECT_EQ(runtime.transport.reverseWebSocket.bindPort, 8600);
    EXPECT_EQ(runtime.transport.connectTimeoutMs, 5000);
}

TEST(ConfigLoaderTest, UsesSafeDefaultsForInvalidOptionalFields)
{
    nlohmann::json document = nlohmann::json::parse(validConfig);
    document["chat"]["worker_threads"] = "many";
    document["features"] = {{"accessibility_chat", "yes"}};

    ConfigLoader loader;
    const ConfigLoadResult result = loader.loadDocument(document);

    EXPECT_TRUE(result.canStart());
    ASSERT_NE(result.config, nullptr);
    EXPECT_EQ(result.config->chat.workerThreads, 4U);
    EXPECT_FALSE(result.config->accessibilityChat);
    EXPECT_TRUE(hasDiagnostic(result, ConfigSeverity::Warning, "chat.worker_threads"));
    EXPECT_TRUE(hasDiagnostic(result, ConfigSeverity::Warning, "features.accessibility_chat"));
}

TEST(ConfigLoaderTest, RejectsMissingActiveTransportButIgnoresUnknownFields)
{
    nlohmann::json document = nlohmann::json::parse(validConfig);
    document["future_extension"] = true;
    document["communication"]["active_transport"] = "missing";

    ConfigLoader loader;
    const ConfigLoadResult result = loader.loadDocument(document);

    EXPECT_FALSE(result.canStart());
    EXPECT_TRUE(hasDiagnostic(result, ConfigSeverity::Warning, "$.future_extension"));
    EXPECT_TRUE(hasDiagnostic(result, ConfigSeverity::Fatal,
                              "communication.transports.missing"));
}

TEST(ConfigLoaderTest, DisablesIncompleteOptionalVoiceFeature)
{
    nlohmann::json document = nlohmann::json::parse(validConfig);
    document["voice"] = {{"enabled", true}, {"host", "http://127.0.0.1"}};

    ConfigLoader loader;
    const ConfigLoadResult result = loader.loadDocument(document);

    EXPECT_TRUE(result.canStart());
    ASSERT_NE(result.config, nullptr);
    EXPECT_FALSE(result.config->voice.enabled);
    EXPECT_TRUE(hasDiagnostic(result, ConfigSeverity::FeatureDisabled, "voice"));
}

TEST(ConfigLoaderTest, MissingOptionalModelSecretDisablesFeatureWithoutBlockingStartup)
{
    nlohmann::json document = nlohmann::json::parse(validConfig);
    document["models"]["drawing"] = {
        {"model", "image-model"},
        {"endpoint", "https://example.invalid"},
        {"api_standard", "OpenAI"},
        {"api_key", {{"from_env", "KLEINBOT_TEST_MISSING_SECRET"}}}
    };

    ConfigLoader loader;
    const ConfigLoadResult result = loader.loadDocument(document);

    EXPECT_TRUE(result.canStart());
    EXPECT_TRUE(hasDiagnostic(result, ConfigSeverity::FeatureDisabled,
                              "models.drawing.api_key"));
}

TEST(ConfigLoaderTest, DecodesHttpEventSignatureSecret)
{
    nlohmann::json document = nlohmann::json::parse(validConfig);
    document["communication"]["active_transport"] = "http";
    document["communication"]["transports"]["http"] = {
        {"type", "http"},
        {"api", {{"base_url", "http://127.0.0.1:3000"}}},
        {"events", {
            {"bind", "127.0.0.1"},
            {"port", 4000},
            {"path", "/onebot/events"},
            {"secret", {{"literal", "event-secret"}}}
        }}
    };

    ConfigLoader loader;
    const ConfigLoadResult result = loader.loadDocument(document);

    ASSERT_TRUE(result.canStart());
    ASSERT_NE(result.config, nullptr);
    EXPECT_EQ(result.config->communication.activeProfile.eventSecret, "event-secret");
    const RuntimeSettings runtime = buildRuntimeSettings(*result.config);
    EXPECT_EQ(runtime.transport.http.eventSignatureSecret, "event-secret");
    EXPECT_TRUE(runtime.transport.http.eventAuthToken.empty());
}

TEST(ConfigLoaderTest, UsesLegacyEventAccessTokenAsSignatureSecret)
{
    nlohmann::json document = nlohmann::json::parse(validConfig);
    document["communication"]["active_transport"] = "http";
    document["communication"]["transports"]["http"] = {
        {"type", "http"},
        {"api", {{"base_url", "http://127.0.0.1:3000"}}},
        {"events", {
            {"port", 4000},
            {"access_token", {{"literal", "legacy-token"}}}
        }}
    };

    ConfigLoader loader;
    const ConfigLoadResult result = loader.loadDocument(document);

    ASSERT_TRUE(result.canStart());
    const RuntimeSettings runtime = buildRuntimeSettings(*result.config);
    EXPECT_EQ(runtime.transport.http.eventAuthToken, "legacy-token");
    EXPECT_EQ(runtime.transport.http.eventSignatureSecret, "legacy-token");
}
