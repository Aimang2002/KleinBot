#include <gtest/gtest.h>

#include "Configuration/ConfigLoader.h"
#include "Configuration/ConfigTemplate.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
class TemporaryConfigPath
{
public:
    TemporaryConfigPath()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("kleinbot-config-template-" + std::to_string(suffix) + ".json");
    }

    ~TemporaryConfigPath()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
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

bool isHexString(const std::string &value)
{
    if (value.size() != 32)
        return false;
    for (const char character : value)
    {
        const bool ok = (character >= '0' && character <= '9') ||
                        (character >= 'a' && character <= 'f');
        if (!ok)
            return false;
    }
    return true;
}
}

TEST(ConfigTemplateTest, GeneratesDistinctHexTokens)
{
    const std::string first = ConfigTemplate::generateWebUiToken();
    const std::string second = ConfigTemplate::generateWebUiToken();
    EXPECT_TRUE(isHexString(first));
    EXPECT_TRUE(isHexString(second));
    EXPECT_NE(first, second);
}

TEST(ConfigTemplateTest, CreateIfMissingGeneratesLoadableConfig)
{
    TemporaryConfigPath file;
    ASSERT_FALSE(std::filesystem::exists(file.path));

    std::string token;
    ASSERT_TRUE(ConfigTemplate::createIfMissing(file.path.string(), token));
    EXPECT_TRUE(isHexString(token));

    ConfigLoader loader;
    const ConfigLoadResult loaded = loader.loadFile(file.path.string());
    EXPECT_TRUE(loaded.canStart());
    ASSERT_NE(loaded.config, nullptr);
    EXPECT_TRUE(loaded.config->webUi.enabled);
    EXPECT_EQ(loaded.config->webUi.bind, "127.0.0.1");
    EXPECT_EQ(loaded.config->webUi.port, kDefaultWebUiPort);
    EXPECT_EQ(loaded.config->webUi.accessToken, token);

    const nlohmann::json document = nlohmann::json::parse(file.read());
    EXPECT_EQ(document["webui"]["access_token"],
              nlohmann::json({{"literal", token}}));

#if !defined(_WIN32)
    using std::filesystem::perms;
    const auto actual = std::filesystem::status(file.path).permissions();
    EXPECT_EQ(actual & perms::mask, perms::owner_read | perms::owner_write);
#endif
}

TEST(ConfigTemplateTest, CreateIfMissingKeepsExistingFile)
{
    TemporaryConfigPath file;
    {
        std::ofstream output(file.path);
        output << "{\"marker\": true}";
    }

    std::string token;
    EXPECT_FALSE(ConfigTemplate::createIfMissing(file.path.string(), token));
    EXPECT_TRUE(token.empty());
    EXPECT_EQ(file.read(), "{\"marker\": true}");
}
