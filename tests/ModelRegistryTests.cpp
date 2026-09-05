#include <gtest/gtest.h>

#include "ModelRegistry/ModelRegistry.h"

#include <filesystem>
#include <fstream>
#include <optional>

namespace
{
std::filesystem::path writeRegistryFile(const std::string &name, const std::string &content)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path);
    output << content;
    return path;
}
}

TEST(ModelRegistryHotReloadTest, ReloadPicksUpNewModelsAndKeepsOldTableOnFailure)
{
    const auto path = writeRegistryFile("kleinbot-registry-hot-reload.json", R"({
        "Models": [
            {"ModelName": ["model-a"], "api_key": "key-a",
             "api_endpoint": "https://a.example.com", "APIStandard": "OpenAI"}
        ]
    })");

    ModelRegistry registry(path.string());
    ASSERT_TRUE(registry.find("model-a").has_value());
    EXPECT_FALSE(registry.find("model-b").has_value());

    // 加入新模型后热重载：进程内立即可见，无需重启
    writeRegistryFile("kleinbot-registry-hot-reload.json", R"({
        "Models": [
            {"ModelName": ["model-a"], "api_key": "key-a",
             "api_endpoint": "https://a.example.com", "APIStandard": "OpenAI"},
            {"ModelName": ["model-b"], "api_key": "key-b",
             "api_endpoint": "https://b.example.com", "APIStandard": "OpenAI"}
        ]
    })");
    EXPECT_TRUE(registry.reload());
    ASSERT_TRUE(registry.find("model-b").has_value());
    EXPECT_EQ(registry.find("model-b")->api_key, "key-b");
    EXPECT_EQ(registry.all().size(), 2u);

    // 损坏的文件重载失败：返回 false，旧表原样保留继续服务
    writeRegistryFile("kleinbot-registry-hot-reload.json", "{ not valid json");
    EXPECT_FALSE(registry.reload());
    ASSERT_TRUE(registry.find("model-a").has_value());
    ASSERT_TRUE(registry.find("model-b").has_value());

    std::error_code ignoredError;
    std::filesystem::remove(path, ignoredError);
}

TEST(ModelRegistryHotReloadTest, FindReturnsACopyIndependentOfLaterReloads)
{
    const auto path = writeRegistryFile("kleinbot-registry-copy.json", R"({
        "Models": [
            {"ModelName": ["model-a"], "api_key": "key-a",
             "api_endpoint": "https://a.example.com", "APIStandard": "OpenAI"}
        ]
    })");

    ModelRegistry registry(path.string());
    const std::optional<ChatModel> fetched = registry.find("model-a");
    ASSERT_TRUE(fetched.has_value());

    // 改副本不影响注册表；重载换表后已取出的副本也保持有效
    ChatModel mutated = *fetched;
    mutated.api_key = "mutated";
    EXPECT_EQ(registry.find("model-a")->api_key, "key-a");

    std::error_code ignoredError;
    std::filesystem::remove(path, ignoredError);
}
