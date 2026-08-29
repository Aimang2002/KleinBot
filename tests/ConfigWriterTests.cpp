#include <gtest/gtest.h>

#include "Configuration/ConfigLoader.h"
#include "Configuration/ConfigWriter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
// 最小有效配置 + 明文/from_env 两种密钥形态与未知键，用于掩码与恢复断言
const char *secretConfig = R"({
    "schema_version": 1,
    "bot": {"id": 10001, "manager_id": 10002, "name": "Klein"},
    "chat": {"default_model": "test-model"},
    "models": {"registry_path": "ModelsName.json"},
    "web_search": {
        "enabled": false,
        "api_key": {"from_env": "KLEIN_CONFIG_WRITER_TEST_ABSENT"}
    },
    "communication": {
        "protocol": {"type": "onebot"},
        "active_transport": "local",
        "transports": {
            "local": {
                "type": "reverse_websocket",
                "bind": "127.0.0.1",
                "port": 8600,
                "access_token": "plain-token"
            }
        }
    },
    "unknown_section": {"note": "keep me"}
})";

class TemporaryConfigFile
{
public:
    TemporaryConfigFile()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("kleinbot-config-writer-" + std::to_string(suffix) + ".json");
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

    std::string read() const
    {
        std::ifstream input(path);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    std::filesystem::path path;
};
}

TEST(ConfigWriterTest, MaskSecretsReplacesAllSecretValues)
{
    const nlohmann::json document = nlohmann::json::parse(secretConfig);
    const nlohmann::json masked = maskSecrets(document);

    EXPECT_EQ(masked["web_search"]["api_key"], kConfigMaskedSentinel);
    EXPECT_EQ(masked["communication"]["transports"]["local"]["access_token"],
              kConfigMaskedSentinel);
    EXPECT_EQ(masked["bot"]["name"], "Klein");
    EXPECT_EQ(masked["communication"]["transports"]["local"]["port"], 8600);
    EXPECT_EQ(masked["unknown_section"], document["unknown_section"]);
}

TEST(ConfigWriterTest, WriteRestoresMaskedSecretsAndPreservesUnknownKeys)
{
    TemporaryConfigFile file;
    file.write(secretConfig);

    nlohmann::json candidate = maskSecrets(nlohmann::json::parse(secretConfig));
    candidate["bot"]["name"] = "Alice";

    ConfigWriter writer;
    const ConfigWriter::Result result = writer.write(file.path.string(), candidate);

    ASSERT_TRUE(result.success);
    const nlohmann::json written = nlohmann::json::parse(file.read());
    EXPECT_EQ(written["bot"]["name"], "Alice");
    EXPECT_EQ(written["web_search"]["api_key"],
              nlohmann::json::parse(R"({"from_env": "KLEIN_CONFIG_WRITER_TEST_ABSENT"})"));
    EXPECT_EQ(written["communication"]["transports"]["local"]["access_token"], "plain-token");
    EXPECT_EQ(written["unknown_section"]["note"], "keep me");
}

TEST(ConfigWriterTest, WriteRejectsInvalidCandidateAndKeepsFile)
{
    TemporaryConfigFile file;
    file.write(secretConfig);

    nlohmann::json candidate = nlohmann::json::parse(secretConfig);
    candidate["bot"].erase("id");

    ConfigWriter writer;
    const ConfigWriter::Result result = writer.write(file.path.string(), candidate);

    EXPECT_FALSE(result.success);
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(file.read(), secretConfig);
}

TEST(ConfigWriterTest, WrittenFileLoadsBackWithTightPermissions)
{
    TemporaryConfigFile file;
    file.write(secretConfig);

    nlohmann::json candidate = nlohmann::json::parse(secretConfig);
    candidate["bot"]["name"] = "Bob";

    ConfigWriter writer;
    ASSERT_TRUE(writer.write(file.path.string(), candidate).success);

    ConfigLoader loader;
    const ConfigLoadResult loaded = loader.loadFile(file.path.string());
    EXPECT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    EXPECT_EQ(loaded.config->bot.name, "Bob");

#if !defined(_WIN32)
    using std::filesystem::perms;
    const auto actual = std::filesystem::status(file.path).permissions();
    EXPECT_EQ(actual & perms::mask, perms::owner_read | perms::owner_write);
#endif
}
