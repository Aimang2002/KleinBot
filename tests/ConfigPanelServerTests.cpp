#include <gtest/gtest.h>

#include "Bootstrap/ConfigSnapshotStore.h"
#include "Configuration/ConfigLoader.h"
#include "Configuration/ConfigWriter.h"
#include "WebUI/ConfigPanelServer.h"
#include "../Library/httplib/httplib.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <stdlib.h>
#endif

namespace
{
// 反向 WS 的 access_token 用 from_env 形态，配合夹具里设置的环境变量，
// 覆盖“掩码往返后 from_env 结构不被压扁”的关键路径；注册表路径用占位符替换为临时目录绝对路径
const char *panelConfig = R"({
    "schema_version": 1,
    "bot": {"id": 10001, "manager_id": 10002, "name": "Klein"},
    "chat": {"default_model": "test-model"},
    "models": {"registry_path": "@REGISTRY_PATH@"},
    "web_search": {
        "enabled": false,
        "api_key": {"literal": "sk-panel-plain-secret"}
    },
    "resources": {
        "help_file": "source/help.txt"
    },
    "communication": {
        "protocol": {"type": "onebot"},
        "active_transport": "local",
        "transports": {
            "local": {
                "type": "reverse_websocket",
                "bind": "127.0.0.1",
                "port": 8600,
                "access_token": {"from_env": "KLEIN_PANEL_TEST_TOKEN"}
            }
        }
    }
})";

void setEnvironmentVariable(const char *name, const char *value)
{
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

class PanelServerFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        setEnvironmentVariable("KLEIN_PANEL_TEST_TOKEN", "onebot-token");
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = std::filesystem::temp_directory_path() /
              ("kleinbot-panel-" + std::to_string(suffix));
        std::filesystem::create_directories(dir);
        configPath = dir / "config.json";
        registryPath = dir / "ModelsName.json";

        std::string content = panelConfig;
        content.replace(content.find("@REGISTRY_PATH@"), strlen("@REGISTRY_PATH@"),
                        registryPath.string());
        writeFile(content);

        ConfigLoader loader;
        const ConfigLoadResult initial = loader.loadText(content);
        ASSERT_TRUE(initial.canStart());
        store = std::make_unique<ConfigSnapshotStore>(configPath.string(), initial.config);

        settings.accessToken = "panel-token";
        settings.bind = "127.0.0.1";
        settings.port = 0;

        server = ConfigPanelServer::buildServer(settings, configPath.string(), *store);
        port = server->bind_to_any_port("127.0.0.1");
        ASSERT_GT(port, 0);
        serverThread = std::make_unique<std::thread>([this]() { server->listen_after_bind(); });
    }

    void TearDown() override
    {
        if (server != nullptr)
            server->stop();
        if (serverThread != nullptr)
            serverThread->join();
        std::error_code error;
        std::filesystem::remove_all(dir, error);
    }

    httplib::Headers authHeaders(const std::string &token = "panel-token") const
    {
        return {{"Authorization", "Bearer " + token}};
    }

    void writeFile(const std::string &content) const
    {
        std::ofstream output(configPath);
        output << content;
    }

    std::string readFile() const
    {
        std::ifstream input(configPath);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    std::string readRegistry() const
    {
        std::ifstream input(registryPath);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    std::filesystem::path dir;
    std::filesystem::path configPath;
    std::filesystem::path registryPath;
    std::unique_ptr<ConfigSnapshotStore> store;
    WebUiSettings settings;
    std::unique_ptr<httplib::Server> server;
    std::unique_ptr<std::thread> serverThread;
    int port = 0;
};
}

TEST_F(PanelServerFixture, GetConfigRejectsMissingOrWrongToken)
{
    {
        httplib::Client client("127.0.0.1", port);
        const auto result = client.Get("/api/config");
        ASSERT_TRUE(result != nullptr);
        EXPECT_EQ(result->status, 401);
    }
    {
        httplib::Client client("127.0.0.1", port);
        const auto result = client.Get("/api/config", authHeaders("wrong-token"));
        ASSERT_TRUE(result != nullptr);
        EXPECT_EQ(result->status, 401);
    }
}

TEST_F(PanelServerFixture, GetConfigMasksSecretValues)
{
    httplib::Client client("127.0.0.1", port);
    const auto result = client.Get("/api/config", authHeaders());
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(result->status, 200);
    EXPECT_EQ(result->body.find("sk-panel-plain-secret"), std::string::npos);
    EXPECT_NE(result->body.find(kConfigMaskedSentinel), std::string::npos);

    const nlohmann::json body = nlohmann::json::parse(result->body);
    EXPECT_EQ(body["bot"]["name"], "Klein");
    EXPECT_EQ(body["communication"]["transports"]["local"]["access_token"],
              kConfigMaskedSentinel);
}

TEST_F(PanelServerFixture, PostSentinelRoundtripWritesFileAndReloadsStore)
{
    {
        httplib::Client client("127.0.0.1", port);
        const auto fetched = client.Get("/api/config", authHeaders());
        ASSERT_TRUE(fetched != nullptr);
        ASSERT_EQ(fetched->status, 200);

        nlohmann::json candidate = nlohmann::json::parse(fetched->body);
        candidate["bot"]["name"] = "Alice";
        const auto posted = client.Post("/api/config", authHeaders(),
                                        candidate.dump(), "application/json");
        ASSERT_TRUE(posted != nullptr);
        ASSERT_EQ(posted->status, 200);

        const nlohmann::json body = nlohmann::json::parse(posted->body);
        EXPECT_TRUE(body["reloadSuccess"].get<bool>());
        EXPECT_EQ(body["counts"]["restart"], 1);
        bool foundBotName = false;
        for (const auto &change : body["diff"])
        {
            if (change["path"] == "bot.name")
                foundBotName = true;
        }
        EXPECT_TRUE(foundBotName);
    }

    const nlohmann::json written = nlohmann::json::parse(readFile());
    EXPECT_EQ(written["bot"]["name"], "Alice");
    EXPECT_EQ(written["web_search"]["api_key"],
              nlohmann::json::parse(R"({"literal": "sk-panel-plain-secret"})"));
    EXPECT_EQ(written["communication"]["transports"]["local"]["access_token"],
              nlohmann::json::parse(R"({"from_env": "KLEIN_PANEL_TEST_TOKEN"})"));
    EXPECT_EQ(store->current()->schema->bot.name, "Alice");
}

TEST_F(PanelServerFixture, PostInvalidConfigReturns422AndKeepsFile)
{
    const std::string before = readFile();

    httplib::Client client("127.0.0.1", port);
    nlohmann::json candidate = nlohmann::json::parse(before);
    candidate["bot"].erase("id");
    const auto posted = client.Post("/api/config", authHeaders(),
                                    candidate.dump(), "application/json");
    ASSERT_TRUE(posted != nullptr);
    EXPECT_EQ(posted->status, 422);

    const nlohmann::json body = nlohmann::json::parse(posted->body);
    EXPECT_FALSE(body["diagnostics"].empty());
    EXPECT_EQ(readFile(), before);
    EXPECT_EQ(store->current()->schema->bot.name, "Klein");
}

TEST_F(PanelServerFixture, RootServesPageFileWithoutAuth)
{
    httplib::Client client("127.0.0.1", port);
    const auto result = client.Get("/");
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(result->status, 200) << "panel.html 未同步到测试工作目录";
    EXPECT_NE(result->body.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(result->body.find("Klein"), std::string::npos);
    EXPECT_NE(result->get_header_value("Cache-Control").find("no-store"), std::string::npos);
}

TEST_F(PanelServerFixture, RootReturns500WhenPageFileMissing)
{
    // ctest 把每个 TEST 注册为独立进程，chdir 只影响本用例；恢复目录以兼容整二进制直跑
    const std::filesystem::path original = std::filesystem::current_path();
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path empty = std::filesystem::temp_directory_path() /
                                        ("kleinbot-panel-empty-" + std::to_string(suffix));
    std::filesystem::create_directories(empty);
    std::filesystem::current_path(empty);

    httplib::Client client("127.0.0.1", port);
    const auto result = client.Get("/");
    std::filesystem::current_path(original);
    std::error_code error;
    std::filesystem::remove_all(empty, error);

    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(result->status, 500);
    EXPECT_NE(result->body.find("panel.html"), std::string::npos);
}

TEST_F(PanelServerFixture, GetModelsReturnsSkeletonWhenFileMissing)
{
    httplib::Client client("127.0.0.1", port);
    const auto result = client.Get("/api/models", authHeaders());
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(result->status, 200);

    const nlohmann::json body = nlohmann::json::parse(result->body);
    EXPECT_EQ(body["registry_path"], registryPath.string());
    EXPECT_FALSE(body["exists"].get<bool>());
    EXPECT_TRUE(body["models"]["Models"].is_array());
    EXPECT_TRUE(body["models"]["Models"].empty());
}

TEST_F(PanelServerFixture, ModelsPostCreatesFileAndMaskedRoundtripKeepsKey)
{
    httplib::Client client("127.0.0.1", port);

    // 首次保存：文件不存在，明文密钥直接创建
    const nlohmann::json creation = nlohmann::json::parse(R"({
        "Models": [
            {"ModelName": ["gpt-test"], "api_endpoint": "https://api.example.com/v1",
             "api_key": "sk-models-secret", "APIStandard": "OpenAI",
             "Capabilities": {"vision": true}}
        ]
    })");
    const auto created = client.Post("/api/models", authHeaders(),
                                     creation.dump(), "application/json");
    ASSERT_TRUE(created != nullptr);
    ASSERT_EQ(created->status, 200);
    const nlohmann::json createdBody = nlohmann::json::parse(created->body);
    EXPECT_TRUE(createdBody["restartRequired"].get<bool>());
    ASSERT_TRUE(std::filesystem::exists(registryPath));

    // GET 掩码：明文不出网
    const auto fetched = client.Get("/api/models", authHeaders());
    ASSERT_TRUE(fetched != nullptr);
    ASSERT_EQ(fetched->status, 200);
    EXPECT_EQ(fetched->body.find("sk-models-secret"), std::string::npos);
    const nlohmann::json fetchedBody = nlohmann::json::parse(fetched->body);
    EXPECT_TRUE(fetchedBody["exists"].get<bool>());
    EXPECT_EQ(fetchedBody["models"]["Models"][0]["api_key"], kConfigMaskedSentinel);

    // POST 掩码往返：改端点，密钥哨兵保持原值
    nlohmann::json candidate = fetchedBody["models"];
    candidate["Models"][0]["api_endpoint"] = "https://api2.example.com/v1";
    const auto posted = client.Post("/api/models", authHeaders(),
                                    candidate.dump(), "application/json");
    ASSERT_TRUE(posted != nullptr);
    ASSERT_EQ(posted->status, 200);

    const nlohmann::json written = nlohmann::json::parse(readRegistry());
    EXPECT_EQ(written["Models"][0]["api_endpoint"], "https://api2.example.com/v1");
    EXPECT_EQ(written["Models"][0]["api_key"], "sk-models-secret");
    EXPECT_EQ(written["Models"][0]["Capabilities"]["vision"], true);
}

TEST_F(PanelServerFixture, PostModelsRejectsDuplicatesAndKeepsFile)
{
    httplib::Client client("127.0.0.1", port);
    const nlohmann::json initial = nlohmann::json::parse(R"({
        "Models": [
            {"ModelName": ["dup-model"], "api_endpoint": "https://a.example.com",
             "api_key": "sk-a", "APIStandard": "OpenAI"}
        ]
    })");
    ASSERT_TRUE(client.Post("/api/models", authHeaders(), initial.dump(), "application/json") != nullptr);
    const std::string before = readRegistry();

    const nlohmann::json duplicate = nlohmann::json::parse(R"({
        "Models": [
            {"ModelName": ["dup-model"], "api_endpoint": "https://a.example.com",
             "api_key": "sk-a", "APIStandard": "OpenAI"},
            {"ModelName": ["dup-model"], "api_endpoint": "https://b.example.com",
             "api_key": "sk-b", "APIStandard": "Anthropic"}
        ]
    })");
    const auto posted = client.Post("/api/models", authHeaders(),
                                    duplicate.dump(), "application/json");
    ASSERT_TRUE(posted != nullptr);
    EXPECT_EQ(posted->status, 422);

    const nlohmann::json body = nlohmann::json::parse(posted->body);
    EXPECT_FALSE(body["diagnostics"].empty());
    EXPECT_EQ(readRegistry(), before);
}

TEST_F(PanelServerFixture, PostModelsDeleteProviderDoesNotLeakKeyAcross)
{
    httplib::Client client("127.0.0.1", port);
    const nlohmann::json initial = nlohmann::json::parse(R"({
        "Models": [
            {"ModelName": ["model-a"], "api_endpoint": "https://a.example.com",
             "api_key": "sk-a", "APIStandard": "OpenAI"},
            {"ModelName": ["model-b"], "api_endpoint": "https://b.example.com",
             "api_key": "sk-b", "APIStandard": "OpenAI"}
        ]
    })");
    ASSERT_TRUE(client.Post("/api/models", authHeaders(), initial.dump(), "application/json") != nullptr);

    const auto fetched = client.Get("/api/models", authHeaders());
    ASSERT_TRUE(fetched != nullptr);
    ASSERT_EQ(fetched->status, 200);
    nlohmann::json candidate = nlohmann::json::parse(fetched->body)["models"];
    candidate["Models"].erase(0); // 删除第一个供应商

    const auto posted = client.Post("/api/models", authHeaders(),
                                    candidate.dump(), "application/json");
    ASSERT_TRUE(posted != nullptr);
    ASSERT_EQ(posted->status, 200);

    const nlohmann::json written = nlohmann::json::parse(readRegistry());
    ASSERT_EQ(written["Models"].size(), 1U);
    EXPECT_EQ(written["Models"][0]["ModelName"][0], "model-b");
    EXPECT_EQ(written["Models"][0]["api_key"], "sk-b");
}

TEST_F(PanelServerFixture, PostModelsRejectsMissingModelsArray)
{
    httplib::Client client("127.0.0.1", port);
    const auto posted = client.Post("/api/models", authHeaders(),
                                    R"({"not_models": []})", "application/json");
    ASSERT_TRUE(posted != nullptr);
    EXPECT_EQ(posted->status, 422);
}
