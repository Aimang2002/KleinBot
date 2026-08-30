#include <gtest/gtest.h>

#include "Asset/ImageAssetStore.h"
#include "ModelApiCaller/Dock.hpp"
#include "ModelApiCaller/ModelEndpointOptions.h"
#include "Tool/GenerateImageTool.h"
#include "Tool/InspectImageTool.h"
#include "Tool/ToolContext.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-tests-XXXXXX";
        pattern.push_back('\0');
        char *created = mkdtemp(pattern.data());
        if (created != nullptr)
            directory = created;
    }

    ~TemporaryDirectory()
    {
        if (!directory.empty())
            std::filesystem::remove_all(directory);
    }

    const std::string &path() const { return directory; }

private:
    std::string directory;
};

ModelEndpointOptions readyEndpoint()
{
    return {"some-model", "https://example.invalid/v1/images", "sk-test", "OpenAI"};
}
}

TEST(ModelEndpointReadinessTest, ReadyEndpointReturnsEmptyText)
{
    EXPECT_TRUE(modelEndpointReadinessText(readyEndpoint(), "生图", "models.drawing").empty());
}

TEST(ModelEndpointReadinessTest, UnconfiguredEndpointExplainsMissingConfiguration)
{
    ModelEndpointOptions empty;
    const std::string text = modelEndpointReadinessText(empty, "生图", "models.drawing");
    EXPECT_NE(text.find("生图"), std::string::npos);
    EXPECT_NE(text.find("尚未配置"), std::string::npos);
    EXPECT_NE(text.find("models.drawing"), std::string::npos);
}

TEST(ModelEndpointReadinessTest, ConfiguredEndpointWithoutKeyExplainsMissingApiKey)
{
    ModelEndpointOptions endpoint = readyEndpoint();
    endpoint.apiKey.clear();
    const std::string text = modelEndpointReadinessText(endpoint, "视觉", "models.vision");
    EXPECT_NE(text.find("视觉"), std::string::npos);
    EXPECT_NE(text.find("API Key"), std::string::npos);
    EXPECT_NE(text.find("models.vision"), std::string::npos);
}

TEST(GenerateImageToolReadinessTest, ExplainsWhenDrawingModelUnconfigured)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    Dock dock(DockOptions{});
    ImageAssetStore store(temporaryDirectory.path() + "/assets.db",
                          temporaryDirectory.path() + "/images");

    GenerateImageTool tool(dock, store, ModelEndpointOptions{});
    const ToolResult result = tool.execute(R"({"prompt":"一只猫"})", ToolContext{});

    EXPECT_NE(result.model_content.find("生图"), std::string::npos);
    EXPECT_NE(result.model_content.find("尚未配置"), std::string::npos);
    EXPECT_TRUE(result.outbound_messages.empty());
    EXPECT_FALSE(result.terminal);
}

TEST(GenerateImageToolReadinessTest, ExplainsWhenDrawingApiKeyMissing)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    Dock dock(DockOptions{});
    ImageAssetStore store(temporaryDirectory.path() + "/assets.db",
                          temporaryDirectory.path() + "/images");

    ModelEndpointOptions endpoint = readyEndpoint();
    endpoint.apiKey.clear();
    GenerateImageTool tool(dock, store, endpoint);
    const ToolResult result = tool.execute(R"({"prompt":"一只猫"})", ToolContext{});

    EXPECT_NE(result.model_content.find("API Key"), std::string::npos);
    EXPECT_TRUE(result.outbound_messages.empty());
    EXPECT_FALSE(result.terminal);
}

TEST(InspectImageToolReadinessTest, ExplainsWhenVisionModelUnconfigured)
{
    TemporaryDirectory temporaryDirectory;
    ASSERT_FALSE(temporaryDirectory.path().empty());
    Dock dock(DockOptions{});
    ImageAssetStore store(temporaryDirectory.path() + "/assets.db",
                          temporaryDirectory.path() + "/images");

    InspectImageTool tool(dock, store, ModelEndpointOptions{});
    const ToolResult result = tool.execute(R"({"question":"图里有什么"})", ToolContext{});

    EXPECT_NE(result.model_content.find("视觉"), std::string::npos);
    EXPECT_NE(result.model_content.find("尚未配置"), std::string::npos);
    EXPECT_TRUE(result.outbound_messages.empty());
    EXPECT_FALSE(result.terminal);
}
