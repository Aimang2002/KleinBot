#include <gtest/gtest.h>

#include "ModelApiCaller/ModelEndpointOptions.h"
#include "ModelApiCaller/EndpointChatModel.h"
#include "ModelApiCaller/ModelEndpointResolver.h"
#include "ModelRegistry/ModelRegistry.h"
#include "utils/Live.h"

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

// 配置实时生效（L1.b）：provider 每次调用先取活值的快照再按注册表解析，
// 配置换了下一次调用即用新值；换值之前拿到的结果不变
TEST(ModelEndpointResolverTest, LiveProviderReadsConfigurationAtEachCall)
{
    RegistryFile file("kleinbot-resolver-live.json", kRegistry);
    ModelRegistry registry(file.string());

    ModelEndpointOptions initial;
    initial.model = "vision-model";
    Live<ModelEndpointOptions> configured(initial);
    const EndpointProvider provider = registryEndpoint(configured, registry);
    const ModelEndpointOptions before = provider();
    EXPECT_EQ(before.endpoint, "https://vision.example.com");
    EXPECT_EQ(before.apiKey, "key-v");

    // 注册表形态换成另一个注册名：下一次调用按新名字解析
    ModelEndpointOptions renamed;
    renamed.model = "claude-x";
    configured.set(renamed);
    const ModelEndpointOptions after = provider();
    EXPECT_EQ(after.model, "claude-x");
    EXPECT_EQ(after.endpoint, "https://anthropic.example.com");
    EXPECT_EQ(after.apiKey, "key-a");
    EXPECT_EQ(after.apiStandard, "Anthropic");
    EXPECT_EQ(before.endpoint, "https://vision.example.com") << "换值之前拿到的结果不变";

    // 换成全配形态：原样直连
    configured.set(ModelEndpointOptions{"vision-model", "https://direct.example.com", "sk-direct", "OpenAI"});
    EXPECT_EQ(provider().endpoint, "https://direct.example.com");
    EXPECT_EQ(provider().apiKey, "sk-direct");

    // 清空：未配置，由调用方的就绪检查处理
    configured.set(ModelEndpointOptions{});
    EXPECT_FALSE(provider().configured());
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

// 配置实时生效 L2.c：judge 每次决策解析一次，调用的端点与记录的模型名出自同一次解析。
// 指定模型注册了就用它；没注册（或没写）就用 worker 端点、记 worker 的名字；worker 也没写
// 模型名时端点未配置（调用方回退默认模型），名字记默认模型
TEST(ModelEndpointResolverTest, LabeledEndpointPairsCalledModelWithRecordedLabel)
{
    RegistryFile file("kleinbot-resolver-labeled.json", kRegistry);
    ModelRegistry registry(file.string());
    ModelEndpointOptions worker;
    worker.model = "vision-model";

    const LabeledEndpoint preferred = resolveLabeledEndpoint("claude-x", worker, "chat-default", registry);
    EXPECT_EQ(preferred.modelLabel, "claude-x");
    EXPECT_EQ(preferred.endpoint.model, "claude-x");
    EXPECT_EQ(preferred.endpoint.endpoint, "https://anthropic.example.com");

    const LabeledEndpoint unregistered =
        resolveLabeledEndpoint("not-registered", worker, "chat-default", registry);
    EXPECT_EQ(unregistered.modelLabel, "vision-model");
    EXPECT_EQ(unregistered.endpoint.model, "vision-model");
    EXPECT_EQ(unregistered.endpoint.endpoint, "https://vision.example.com");

    const LabeledEndpoint followsWorker = resolveLabeledEndpoint("", worker, "chat-default", registry);
    EXPECT_EQ(followsWorker.modelLabel, followsWorker.endpoint.model);

    const LabeledEndpoint fallsBack =
        resolveLabeledEndpoint("", ModelEndpointOptions{}, "chat-default", registry);
    EXPECT_FALSE(fallsBack.endpoint.configured());
    EXPECT_EQ(fallsBack.modelLabel, "chat-default");
}
