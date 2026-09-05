#include <gtest/gtest.h>

#include "Bootstrap/ConfigSnapshotStore.h"
#include "Configuration/ConfigLoader.h"
#include "Configuration/ConfigWriter.h"
#include "ModelRegistry/ModelRegistry.h"
#include "WebUI/ConfigPanelServer.h"
#include "../Library/httplib/httplib.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <stdlib.h>
#endif

namespace
{
// 反向 WS 的 access_token 用 from_env 形态，配合夹具里设置的环境变量，
// 覆盖“掩码往返后 from_env 结构不被压扁”的关键路径；注册表路径已钉死为
// kModelRegistryPath（相对工作目录），夹具 chdir 进临时目录隔离
const char *panelConfig = R"({
    "schema_version": 1,
    "bot": {"id": 10001, "manager_id": 10002, "name": "Klein"},
    "chat": {"default_model": "test-model"},
    "models": {},
    "web_search": {
        "enabled": false,
        "api_key": {"literal": "sk-panel-plain-secret"}
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
        originalCwd = std::filesystem::current_path();
        dir = std::filesystem::temp_directory_path() /
              ("kleinbot-panel-" + std::to_string(suffix));
        std::filesystem::create_directories(dir / "source" / "Model");
        configPath = dir / "config.json";
        registryPath = dir / "source" / "Model" / "ModelsName.json";

        writeFile(panelConfig);

        // GET / 从工作目录现读 panel.html：夹具 chdir 后要在临时目录里备一份
        std::error_code copyError;
        std::filesystem::copy_file(originalCwd / "panel.html", dir / "panel.html",
                                   std::filesystem::copy_options::overwrite_existing,
                                   copyError);
        ASSERT_FALSE(copyError) << "panel.html 未同步到测试工作目录";

        // 注册表路径钉死且相对工作目录：整个用例在临时目录里运行
        std::filesystem::current_path(dir);
        registry = std::make_unique<ModelRegistry>(std::string(kModelRegistryPath));

        ConfigLoader loader;
        const ConfigLoadResult initial = loader.loadText(panelConfig);
        ASSERT_TRUE(initial.canStart());
        store = std::make_unique<ConfigSnapshotStore>(configPath.string(), initial.config);

        settings.accessToken = "panel-token";
        settings.bind = "127.0.0.1";
        settings.port = 0;

        server = ConfigPanelServer::buildServer(settings, configPath.string(), *store, *registry);
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
        std::filesystem::current_path(originalCwd);
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
    std::filesystem::path originalCwd;
    std::filesystem::path configPath;
    std::filesystem::path registryPath;
    std::unique_ptr<ConfigSnapshotStore> store;
    std::unique_ptr<ModelRegistry> registry;
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
    EXPECT_EQ(body["registry_path"], std::string(kModelRegistryPath));
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
    EXPECT_TRUE(createdBody["hotReloaded"].get<bool>());
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

TEST_F(PanelServerFixture, PostModelsHotReloadsInProcessRegistry)
{
    httplib::Client client("127.0.0.1", port);
    EXPECT_FALSE(registry->find("gpt-hot").has_value());

    const nlohmann::json creation = nlohmann::json::parse(R"({
        "Models": [
            {"ModelName": ["gpt-hot"], "api_endpoint": "https://api.example.com/v1",
             "api_key": "sk-hot", "APIStandard": "OpenAI"}
        ]
    })");
    const auto created = client.Post("/api/models", authHeaders(),
                                     creation.dump(), "application/json");
    ASSERT_TRUE(created != nullptr);
    ASSERT_EQ(created->status, 200);

    // 写盘成功后进程内副本即时可见，无需重启
    const std::optional<ChatModel> model = registry->find("gpt-hot");
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->api_key, "sk-hot");
    EXPECT_EQ(model->endpoint, "https://api.example.com/v1");
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

// 勾选模型改动 ModelName 后，身份匹配（只认数组字段相等）会失配；
// 前端带回的 originIndexes 必须把掩码密钥按位恢复回去
TEST_F(PanelServerFixture, PostModelsRestoresMaskedKeyByOriginIndex)
{
    httplib::Client client("127.0.0.1", port);
    const nlohmann::json initial = nlohmann::json::parse(R"({
        "Models": [
            {"ModelName": ["model-a"], "api_endpoint": "https://a.example.com",
             "api_key": "sk-a", "APIStandard": "OpenAI", "describe": "A"}
        ]
    })");
    ASSERT_TRUE(client.Post("/api/models", authHeaders(), initial.dump(),
                            "application/json") != nullptr);

    const auto fetched = client.Get("/api/models", authHeaders());
    ASSERT_TRUE(fetched != nullptr);
    ASSERT_EQ(fetched->status, 200);
    nlohmann::json candidate = nlohmann::json::parse(fetched->body)["models"];
    candidate["Models"][0]["ModelName"] = nlohmann::json{"model-a2", "model-a3"};
    candidate["Models"][0]["describe"] = "A(改名)";
    // 与前端 collectModels 一致：带回各卡片的注册表原始下标
    candidate["originIndexes"] = nlohmann::json{0};
    const auto posted = client.Post("/api/models", authHeaders(),
                                    candidate.dump(), "application/json");
    ASSERT_TRUE(posted != nullptr);
    ASSERT_EQ(posted->status, 200);

    const nlohmann::json body = nlohmann::json::parse(posted->body);
    EXPECT_TRUE(body["warnings"].empty());

    const nlohmann::json written = nlohmann::json::parse(readRegistry());
    EXPECT_EQ(written["Models"][0]["api_key"], "sk-a");
    EXPECT_EQ(written["Models"][0]["ModelName"],
              (nlohmann::json{"model-a2", "model-a3"}));
    EXPECT_EQ(written["Models"][0]["describe"], "A(改名)");
    // originIndexes 是请求辅助字段，不能落盘
    EXPECT_EQ(written["Models"][0].contains("originIndexes"), false);
    EXPECT_EQ(written.contains("originIndexes"), false);
}

// 本地 mock 供应商端点：验证面板代拉模型列表/视觉探测的全链路（含 curl 外呼与密钥回退）
class MockProvider
{
public:
    MockProvider()
    {
        server.Get("/v1/models", [](const httplib::Request &, httplib::Response &response) {
            if (requestedBearer != "sk-good-key")
            {
                response.status = 401;
                return;
            }
            response.set_content(R"({"data": [{"id": "mock-a"}, {"id": "mock-b"}]})",
                                 "application/json");
        });
        // 视觉探测：按请求体里的模型名返回不同结论
        server.Post("/v1/chat/completions",
                    [](const httplib::Request &request, httplib::Response &response) {
            if (request.body.find("vision-ok") != std::string::npos)
            {
                response.set_content(
                    R"({"choices":[{"message":{"role":"assistant","content":"OK"}}]})",
                    "application/json");
                return;
            }
            if (request.body.find("vision-no") != std::string::npos)
            {
                response.status = 400;
                response.set_content(
                    R"({"error":{"message":"Image input is not supported for this model"}})",
                    "application/json");
                return;
            }
            if (request.body.find("vision-unknown") != std::string::npos)
            {
                response.status = 400;
                response.set_content(R"({"error":{"message":"model not found"}})",
                                     "application/json");
                return;
            }
            response.status = 401;
        });
        server.set_pre_routing_handler([](const httplib::Request &request, httplib::Response &) {
            if (request.has_header("Authorization"))
                requestedBearer = request.get_header_value("Authorization").substr(7);
            else
                requestedBearer.clear();
            return httplib::Server::HandlerResponse::Unhandled;
        });
        port = server.bind_to_any_port("127.0.0.1");
        thread = std::thread([this]() { server.listen_after_bind(); });
    }

    ~MockProvider()
    {
        server.stop();
        thread.join();
    }

    std::string chatEndpoint() const
    {
        return "http://127.0.0.1:" + std::to_string(port) + "/v1/chat/completions";
    }

    httplib::Server server;
    std::thread thread;
    int port = 0;
    inline static std::string requestedBearer;
};

TEST_F(PanelServerFixture, AvailableModelsRejectsInvalidRequestBeforeOutboundCall)
{
    httplib::Client client("127.0.0.1", port);

    const auto missingEndpoint = client.Post("/api/models/available", authHeaders(),
                                             R"({"APIStandard": "OpenAI"})",
                                             "application/json");
    ASSERT_TRUE(missingEndpoint != nullptr);
    EXPECT_EQ(missingEndpoint->status, 422);

    const auto badStandard = client.Post("/api/models/available", authHeaders(),
                                         R"({"api_endpoint": "https://x.example.com/v1/chat/completions",
                                            "APIStandard": "Gemini"})",
                                         "application/json");
    ASSERT_TRUE(badStandard != nullptr);
    EXPECT_EQ(badStandard->status, 422);

    // 无法推导列表地址
    const auto underivable = client.Post("/api/models/available", authHeaders(),
                                         R"({"api_endpoint": "https://x.example.com/v1/foo",
                                            "APIStandard": "OpenAI", "api_key": "sk-k"})",
                                         "application/json");
    ASSERT_TRUE(underivable != nullptr);
    EXPECT_EQ(underivable->status, 422);

    // 已存注册表也没有密钥可用
    const auto missingKey = client.Post("/api/models/available", authHeaders(),
                                        R"({"api_endpoint": "https://x.example.com/v1/messages",
                                           "APIStandard": "Anthropic"})",
                                        "application/json");
    ASSERT_TRUE(missingKey != nullptr);
    EXPECT_EQ(missingKey->status, 422);
    EXPECT_NE(missingKey->body.find("API Key"), std::string::npos);
}

TEST_F(PanelServerFixture, AvailableModelsFetchesFromEndpointWithStoredKeyFallback)
{
    // 预存一个供应商：请求不带密钥时从注册表回退取用
    {
        std::ofstream output(registryPath);
        output << R"({
            "Models": [
                {"ModelName": ["mock-a"], "api_key": "sk-good-key", "APIStandard": "OpenAI",
                 "describe": "Mock 供应商"}
            ]
        })";
    }

    MockProvider provider;
    const nlohmann::json payload = {
        {"api_endpoint", provider.chatEndpoint()},
        {"APIStandard", "OpenAI"},
        {"index", 0},
    };
    httplib::Client client("127.0.0.1", port);
    const auto result = client.Post("/api/models/available", authHeaders(),
                                    payload.dump(), "application/json");

    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(result->status, 200);
    const nlohmann::json fetched = nlohmann::json::parse(result->body);
    EXPECT_EQ(fetched["models"], (nlohmann::json{"mock-a", "mock-b"}));
    EXPECT_EQ(MockProvider::requestedBearer, "sk-good-key");
}

TEST_F(PanelServerFixture, AvailableModelsPrefersExplicitKeyAndReportsUpstreamErrors)
{
    MockProvider provider;
    httplib::Client client("127.0.0.1", port);

    const nlohmann::json payload = {
        {"api_endpoint", provider.chatEndpoint()},
        {"APIStandard", "OpenAI"},
        {"api_key", "sk-wrong-key"},
    };
    const auto wrongKey = client.Post("/api/models/available", authHeaders(),
                                      payload.dump(), "application/json");
    ASSERT_TRUE(wrongKey != nullptr);
    EXPECT_EQ(wrongKey->status, 502);
    EXPECT_NE(wrongKey->body.find("401"), std::string::npos);
    EXPECT_EQ(MockProvider::requestedBearer, "sk-wrong-key");
}

// 视觉探测端点：同一供应商的三个模型分别得出 支持/不支持/无法判定 三态
TEST_F(PanelServerFixture, CheckVisionClassifiesProbeOutcomes)
{
    MockProvider provider;
    const nlohmann::json payloadBase = {
        {"api_endpoint", provider.chatEndpoint()},
        {"APIStandard", "OpenAI"},
        {"api_key", "sk-good-key"},
    };
    httplib::Client client("127.0.0.1", port);

    const auto probe = [&](const std::string &model) {
        nlohmann::json payload = payloadBase;
        payload["model"] = model;
        return client.Post("/api/models/check-vision", authHeaders(),
                           payload.dump(), "application/json");
    };

    const auto supported = probe("vision-ok");
    ASSERT_TRUE(supported != nullptr);
    ASSERT_EQ(supported->status, 200);
    EXPECT_EQ(nlohmann::json::parse(supported->body)["result"], "vision");

    const auto unsupported = probe("vision-no");
    ASSERT_TRUE(unsupported != nullptr);
    ASSERT_EQ(unsupported->status, 200);
    EXPECT_EQ(nlohmann::json::parse(unsupported->body)["result"], "no-vision");

    const auto unknown = probe("vision-unknown");
    ASSERT_TRUE(unknown != nullptr);
    ASSERT_EQ(unknown->status, 200);
    const nlohmann::json unknownBody = nlohmann::json::parse(unknown->body);
    EXPECT_EQ(unknownBody["result"], "unknown");
    EXPECT_FALSE(unknownBody["detail"].get<std::string>().empty());
}

TEST_F(PanelServerFixture, CheckVisionRejectsInvalidRequests)
{
    httplib::Client client("127.0.0.1", port);
    const auto post = [&](const std::string &body) {
        return client.Post("/api/models/check-vision", authHeaders(),
                           body, "application/json");
    };

    const auto missingModel = post(R"({"api_endpoint": "https://x.example.com/v1/chat/completions",
                                    "APIStandard": "OpenAI", "api_key": "sk-k"})");
    ASSERT_TRUE(missingModel != nullptr);
    EXPECT_EQ(missingModel->status, 422);

    const auto badStandard = post(R"({"api_endpoint": "https://x.example.com/v1/chat/completions",
                                    "APIStandard": "Gemini", "model": "m", "api_key": "sk-k"})");
    ASSERT_TRUE(badStandard != nullptr);
    EXPECT_EQ(badStandard->status, 422);

    const auto missingKey = post(R"({"api_endpoint": "https://x.example.com/v1/chat/completions",
                                  "APIStandard": "OpenAI", "model": "m"})");
    ASSERT_TRUE(missingKey != nullptr);
    EXPECT_EQ(missingKey->status, 422);
}

// Capabilities.vision 双形态校验：非法类型拒绝；名单含未知模型名给 Warning 放行
TEST_F(PanelServerFixture, PostModelsValidatesVisionCapabilityForms)
{
    httplib::Client client("127.0.0.1", port);

    const nlohmann::json badType = nlohmann::json::parse(R"({
        "Models": [
            {"ModelName": ["model-a"], "api_endpoint": "https://a.example.com",
             "api_key": "sk-a", "APIStandard": "OpenAI",
             "Capabilities": {"vision": "yes"}}
        ]
    })");
    const auto rejected = client.Post("/api/models", authHeaders(),
                                      badType.dump(), "application/json");
    ASSERT_TRUE(rejected != nullptr);
    EXPECT_EQ(rejected->status, 422);

    const nlohmann::json withTypo = nlohmann::json::parse(R"({
        "Models": [
            {"ModelName": ["model-a"], "api_endpoint": "https://a.example.com",
             "api_key": "sk-a", "APIStandard": "OpenAI",
             "Capabilities": {"vision": ["model-a", "typo-model"]}}
        ]
    })");
    const auto accepted = client.Post("/api/models", authHeaders(),
                                      withTypo.dump(), "application/json");
    ASSERT_TRUE(accepted != nullptr);
    ASSERT_EQ(accepted->status, 200);
    const nlohmann::json body = nlohmann::json::parse(accepted->body);
    ASSERT_FALSE(body["warnings"].empty());
    EXPECT_NE(body["warnings"].dump().find("typo-model"), std::string::npos);
}
