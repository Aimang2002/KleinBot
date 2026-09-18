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
    // 观察通道不进配置骨架（用户定规 2026-09-15）：开关与监控群集是运行时
    // 状态，由数据库独占管理，配置体系不再有 perception 节
    EXPECT_FALSE(document.contains("perception"));

    // 骨架包含全部配置节（2026-09-19 用户定规：不给用户不完整的内容），
    // 面板首跑即可见所有模块；值为安全占位——密钥空串而非 from_env，
    // manager_id 为 0（未设置），不预置假管理员
    for (const char *section : {"bot", "chat", "models", "voice", "memory", "web_search",
                                "web_fetch", "storage", "network", "communication", "webui"})
        EXPECT_TRUE(document.contains(section)) << section;
    EXPECT_EQ(document["chat"]["temperature"], 1.0);
    EXPECT_EQ(document["chat"]["max_message_tokens"], 4096);
    EXPECT_EQ(document["models"]["drawing"]["api_key"], "");
    EXPECT_EQ(document["web_search"]["max_results"], 5);
    EXPECT_EQ(document["bot"]["manager_id"], 0);

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
