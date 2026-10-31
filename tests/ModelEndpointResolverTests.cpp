#include <gtest/gtest.h>

#include "ModelApiCaller/ModelEndpointOptions.h"
#include "ModelApiCaller/EndpointChatModel.h"
#include "ModelApiCaller/ModelEndpointResolver.h"
#include "ModelRegistry/ModelRegistry.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
std::filesystem::path writeRegistryFile(const std::string &name, const std::string &content)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path);
    output << content;
    return path;
}

class RegistryFile
{
public:
    RegistryFile(const std::string &name, const std::string &content)
        : path(writeRegistryFile(name, content)) {}
    ~RegistryFile()
    {
        std::error_code ignoredError;
        std::filesystem::remove(path, ignoredError);
    }
    void rewrite(const std::string &content) const { writeRegistryFile(path.filename().string(), content); }
    std::string string() const { return path.string(); }

private:
    std::filesystem::path path;
};

const char *kRegistry = R"({
    "Models": [
        {"ModelName": ["vision-model", "vision-model-mini"], "api_key": "key-v",
         "api_endpoint": "https://vision.example.com", "APIStandard": "OpenAI"},
        {"ModelName": ["claude-x"], "api_key": "key-a",
         "api_endpoint": "https://anthropic.example.com", "APIStandard": "Anthropic"}
    ]
})";
} // namespace

TEST(ModelEndpointResolverTest, FillsEndpointKeyAndStandardFromRegistryWhenOnlyModelConfigured)
{
    RegistryFile file("kleinbot-resolver-fill.json", kRegistry);
    ModelRegistry registry(file.string());

    ModelEndpointOptions configured;
    configured.model = "vision-model-mini";
    const ModelEndpointOptions resolved = resolveEndpoint(configured, registry);

    EXPECT_EQ(resolved.model, "vision-model-mini");
    EXPECT_EQ(resolved.endpoint, "https://vision.example.com");
    EXPECT_EQ(resolved.apiKey, "key-v");
    EXPECT_EQ(resolved.apiStandard, "OpenAI");
    EXPECT_TRUE(resolved.configured());
    EXPECT_TRUE(modelEndpointReadinessText(resolved, "视觉", "models.vision").empty())
        << "就绪检查跑在解析结果上：注册表形态不应被报未配置";
}

TEST(ModelEndpointResolverTest, KeepsExplicitEndpointUntouched)
{
    RegistryFile file("kleinbot-resolver-explicit.json", kRegistry);
    ModelRegistry registry(file.string());

    // 名字在注册表里也一样：显式端点优先，原样直连
    const ModelEndpointOptions configured{"vision-model", "https://direct.example.com", "sk-direct", "Anthropic"};
    const ModelEndpointOptions resolved = resolveEndpoint(configured, registry);

    EXPECT_EQ(resolved.model, "vision-model");
    EXPECT_EQ(resolved.endpoint, "https://direct.example.com");
    EXPECT_EQ(resolved.apiKey, "sk-direct");
    EXPECT_EQ(resolved.apiStandard, "Anthropic");
}

TEST(ModelEndpointResolverTest, UnknownOrEmptyModelStaysUnresolved)
{
    RegistryFile file("kleinbot-resolver-unknown.json", kRegistry);
    ModelRegistry registry(file.string());

    ModelEndpointOptions unknown;
    unknown.model = "not-registered";
    const ModelEndpointOptions resolved = resolveEndpoint(unknown, registry);
    EXPECT_EQ(resolved.model, "not-registered");
    EXPECT_TRUE(resolved.endpoint.empty());
    EXPECT_TRUE(resolved.apiKey.empty());
    EXPECT_TRUE(resolved.apiStandard.empty());
    EXPECT_FALSE(resolved.configured());
    EXPECT_FALSE(modelEndpointReadinessText(resolved, "视觉", "models.vision").empty());

    const ModelEndpointOptions empty = resolveEndpoint(ModelEndpointOptions{}, registry);
    EXPECT_TRUE(empty.model.empty());
    EXPECT_TRUE(empty.endpoint.empty());
}

TEST(ModelEndpointResolverTest, RegistryProviderResolvesAtCallTime)
{
    RegistryFile file("kleinbot-resolver-provider.json", R"({"Models": []})");
    ModelRegistry registry(file.string());

    ModelEndpointOptions configured;
    configured.model = "late-model";
    const EndpointProvider provider = registryEndpoint(configured, registry);
    EXPECT_TRUE(provider().endpoint.empty()) << "未注册时保持空端点";

    // 注册表热重载后，同一个 provider 下一次调用即拿到新端点
    file.rewrite(R"({
        "Models": [
            {"ModelName": ["late-model"], "api_key": "key-late",
             "api_endpoint": "https://late.example.com", "APIStandard": "OpenAI"}
        ]
    })");
    ASSERT_TRUE(registry.reload());
    EXPECT_EQ(provider().endpoint, "https://late.example.com");
    EXPECT_EQ(provider().apiKey, "key-late");
}

TEST(ModelEndpointResolverTest, FixedAndEmptyProviders)
{
    const ModelEndpointOptions options{"m", "https://e.example.com", "k", "OpenAI"};
    const EndpointProvider fixed = fixedEndpoint(options);
    EXPECT_EQ(currentEndpoint(fixed).endpoint, "https://e.example.com");
    EXPECT_EQ(currentEndpoint(fixed).model, "m");

    const EndpointProvider none;
    EXPECT_FALSE(currentEndpoint(none).configured());
}

TEST(ModelEndpointResolverTest, ToChatModelMapsConnectionFields)
{
    const ModelEndpointOptions options{"model-name", "https://e.example.com", "sk-1", "Anthropic"};
    const ChatModel model = toChatModel(options);
    EXPECT_EQ(model.endpoint, "https://e.example.com");
    EXPECT_EQ(model.api_key, "sk-1");
    EXPECT_EQ(model.api_standard, "Anthropic");
    EXPECT_TRUE(model.modelList.empty()) << "模型名不进 ChatModel，单独作 model_name 传给 Dock";
    EXPECT_TRUE(model.visionModels.empty());
}
