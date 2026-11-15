#include <gtest/gtest.h>

#include "Bootstrap/ConfigReloader.h"
#include "Bootstrap/ConfigRules.h"
#include "Bootstrap/ConfigSnapshotStore.h"
#include "Bootstrap/LiveSettings.h"
#include "Configuration/ConfigLoader.h"
#include "ModelApiCaller/ModelEndpointResolver.h"
#include "ModelRegistry/ModelRegistry.h"
#include "Persistence/ConversationStore.h"
#include "UserSession/UserSessionService.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
const char *kBaseConfig = R"({
    "schema_version": 1,
    "bot": {"id": 10001, "manager_id": 10002, "name": "Klein"},
    "chat": {"default_model": "test-model"},
    "models": {},
    "communication": {
        "protocol": {"type": "onebot"},
        "active_transport": "local",
        "transports": {
            "local": {"type": "reverse_websocket", "bind": "127.0.0.1", "port": 8600}
        }
    }
})";

// 测试用应用函数：计数，并把对应的一组换进活值
std::atomic<int> chatApplyCalls{0};
std::atomic<int> voiceApplyCalls{0};

void applyChat(LiveSettings &live, const RuntimeSettings &next)
{
    chatApplyCalls.fetch_add(1);
    live.chat.set(next.chat);
}

void applyVoice(LiveSettings &live, const RuntimeSettings &next)
{
    voiceApplyCalls.fetch_add(1);
    live.voice.set(next.voice);
}

// 小表：两行共用 applyChat，验证同一个应用函数一次重载只调一次
const ConfigRule kApplyingRules[] = {
    {"chat.temperature",    applies(applyChat),                 field<&SchemaConfig::chat, &ChatSchema::temperature>},
    {"chat.top_p",          applies(applyChat),                 field<&SchemaConfig::chat, &ChatSchema::topP>},
    {"voice.host",          applies(applyVoice),                field<&SchemaConfig::voice, &VoiceSchema::host>},
    {"memory.batch_turns",  notYet(ConfigChangeImpact::Rebuild), field<&SchemaConfig::memory, &MemorySchema::batchTurns>},
    {"chat.worker_threads", restart(),                          field<&SchemaConfig::chat, &ChatSchema::workerThreads>},
};

// 并发用例：比较函数总报“变了”，应用函数检测有没有两次应用同时进行
std::atomic<int> slowInside{0};
std::atomic<int> slowOverlaps{0};
std::atomic<int> slowCalls{0};

bool alwaysChanged(const SchemaConfig &, const SchemaConfig &)
{
    return true;
}

void applySlowly(LiveSettings &live, const RuntimeSettings &next)
{
    if (slowInside.fetch_add(1) != 0)
        slowOverlaps.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    live.chat.set(next.chat);
    slowInside.fetch_sub(1);
    slowCalls.fetch_add(1);
}

const ConfigRule kSlowRules[] = {
    {"chat.temperature", applies(applySlowly), alwaysChanged},
};

// 全部活值的快照：持有 shared_ptr，旧快照不会被释放，指针比较不会撞上复用的地址
struct LiveSnapshots
{
    Live<ChatOptions>::Snapshot chat;
    Live<ModelEndpointOptions>::Snapshot drawingModel;
    Live<ModelEndpointOptions>::Snapshot visionModel;
    Live<ModelEndpointOptions>::Snapshot workerModel;
    Live<VoiceOptions>::Snapshot voice;
    Live<MemoryOptions>::Snapshot memory;
    Live<WebSearchOptions>::Snapshot webSearch;
    Live<WebFetchOptions>::Snapshot webFetch;
    Live<DockOptions>::Snapshot dock;
    Live<GlanceOptions>::Snapshot glance;
    Live<MessageOptions>::Snapshot message;
};

LiveSnapshots snapshotsOf(const LiveSettings &live)
{
    return {live.chat.get(),         live.drawingModel.get(), live.visionModel.get(),
            live.workerModel.get(),  live.voice.get(),        live.memory.get(),
            live.webSearch.get(),    live.webFetch.get(),     live.dock.get(),
            live.glance.get(),       live.message.get()};
}

void expectAllUnchanged(const LiveSnapshots &before, const LiveSettings &live)
{
    const LiveSnapshots now = snapshotsOf(live);
    EXPECT_EQ(now.chat, before.chat);
    EXPECT_EQ(now.drawingModel, before.drawingModel);
    EXPECT_EQ(now.visionModel, before.visionModel);
    EXPECT_EQ(now.workerModel, before.workerModel);
    EXPECT_EQ(now.voice, before.voice);
    EXPECT_EQ(now.memory, before.memory);
    EXPECT_EQ(now.webSearch, before.webSearch);
    EXPECT_EQ(now.webFetch, before.webFetch);
    EXPECT_EQ(now.dock, before.dock);
    EXPECT_EQ(now.glance, before.glance);
    EXPECT_EQ(now.message, before.message);
}

class ConfigReloaderFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("kleinbot-config-reloader-" + std::to_string(suffix) + ".json");
        write(kBaseConfig);
        const ConfigLoadResult initial = ConfigLoader().loadText(kBaseConfig);
        ASSERT_TRUE(initial.canStart());
        store = std::make_unique<ConfigSnapshotStore>(path.string(), initial.config);
        live = std::make_unique<LiveSettings>(store->current()->runtime);
        chatApplyCalls = 0;
        voiceApplyCalls = 0;
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    void write(const std::string &content) const
    {
        std::ofstream output(path);
        output << content;
    }

    void writeEdited(const std::function<void(nlohmann::json &)> &edit) const
    {
        nlohmann::json document = nlohmann::json::parse(kBaseConfig);
        edit(document);
        write(document.dump());
    }

    std::filesystem::path path;
    std::unique_ptr<ConfigSnapshotStore> store;
    std::unique_ptr<LiveSettings> live;
};
}

// 校验不过：快照、活值都不变，不调用任何应用函数
TEST_F(ConfigReloaderFixture, FailedReloadAppliesNothing)
{
    ConfigReloader reloader(*store, *live, ruleTable(kApplyingRules));
    // 候选里带应用函数的那一行变了，但文件被截断、不是合法 JSON
    nlohmann::json candidate = nlohmann::json::parse(kBaseConfig);
    candidate["chat"]["temperature"] = 0.3;
    const std::string text = candidate.dump();
    write(text.substr(0, text.size() - 1));
    const std::shared_ptr<const ConfigSnapshot> snapshotBefore = store->current();
    const LiveSnapshots before = snapshotsOf(*live);

    const ConfigReloadReport report = reloader.reload();

    EXPECT_FALSE(report.result.success);
    EXPECT_FALSE(report.result.diagnostics.empty());
    EXPECT_EQ(store->current(), snapshotBefore);
    expectAllUnchanged(before, *live);
    EXPECT_EQ(chatApplyCalls.load(), 0);
    EXPECT_EQ(voiceApplyCalls.load(), 0);
    EXPECT_TRUE(report.applied.empty());
    EXPECT_TRUE(report.restartOnly.empty());
    EXPECT_TRUE(report.notWired.empty());
    EXPECT_EQ(summarizeReload(report), "配置刷新失败，继续使用当前内存快照。");
}

// 能解析、但诊断里有 Error/Fatal 的候选同样校验不过：不发布、不应用、反馈失败
TEST_F(ConfigReloaderFixture, ParsedButInvalidCandidateAppliesNothing)
{
    struct BadCandidate
    {
        const char *name;
        std::function<void(nlohmann::json &)> breakIt;
        ConfigSeverity severity;
    };
    const BadCandidate candidates[] = {
        {"缺必填项", [](nlohmann::json &config) { config["bot"].erase("id"); },
         ConfigSeverity::Fatal},
        {"路径不以 / 开头",
         [](nlohmann::json &config) { config["communication"]["transports"]["local"]["path"] = "onebot"; },
         ConfigSeverity::Error},
    };
    ConfigReloader reloader(*store, *live, ruleTable(kApplyingRules));
    for (const BadCandidate &bad : candidates)
    {
        writeEdited([&bad](nlohmann::json &config) {
            config["chat"]["temperature"] = 0.3; // 带应用函数的行变了
            bad.breakIt(config);
        });
        const std::shared_ptr<const ConfigSnapshot> snapshotBefore = store->current();
        const LiveSnapshots before = snapshotsOf(*live);

        const ConfigReloadReport report = reloader.reload();

        EXPECT_FALSE(report.result.success) << bad.name;
        bool found = false;
        for (const ConfigDiagnostic &diagnostic : report.result.diagnostics)
        {
            if (diagnostic.severity == bad.severity)
                found = true;
        }
        EXPECT_TRUE(found) << bad.name;
        EXPECT_EQ(store->current(), snapshotBefore) << bad.name;
        expectAllUnchanged(before, *live);
        EXPECT_EQ(chatApplyCalls.load(), 0) << bad.name;
        EXPECT_TRUE(report.applied.empty()) << bad.name;
        EXPECT_TRUE(report.restartOnly.empty()) << bad.name;
        EXPECT_TRUE(report.notWired.empty()) << bad.name;
        EXPECT_EQ(summarizeReload(report), "配置刷新失败，继续使用当前内存快照。") << bad.name;
    }
}

// 产品规则表（默认）：只有接通的行把新值换进活值（L1.a 起 chat 组 6 个键，L1.b 起
// models 的 12 个键），其余活值的快照指针不变；反馈如实列出已生效、需重启与尚未接通的键
TEST_F(ConfigReloaderFixture, ProductRulesApplyOnlyWiredRows)
{
    ConfigReloader reloader(*store, *live);
    writeEdited([](nlohmann::json &config) {
        config["bot"]["group_chat_enabled"] = false;
        config["chat"]["temperature"] = 0.3;
        config["chat"]["worker_threads"] = 3;
        config["models"]["vision"] = {{"model", "vision-next"}};
        config["voice"] = {{"enabled", true}, {"host", "127.0.0.1"}, {"port", "9880"}};
        config["storage"] = {{"conversation_database", "next.db"}};
        config["network"] = {{"proxy", "http://127.0.0.1:7890"}};
    });
    const LiveSnapshots before = snapshotsOf(*live);

    const ConfigReloadReport report = reloader.reload();

    ASSERT_TRUE(report.result.success);
    EXPECT_EQ(report.result.diff.size(), 9U);
    EXPECT_EQ(store->current(), report.result.snapshot);
    EXPECT_EQ(report.applied, (std::vector<std::string>{"chat.temperature", "models.vision.model"}));
    EXPECT_EQ(report.restartOnly,
              (std::vector<std::string>{"chat.worker_threads", "storage.conversation_database"}));
    EXPECT_EQ(report.notWired,
              (std::vector<std::string>{"bot.group_chat_enabled", "voice.enabled", "voice.host",
                                        "voice.port", "network.proxy"}));

    const LiveSnapshots after = snapshotsOf(*live);
    EXPECT_NE(after.chat, before.chat);
    EXPECT_DOUBLE_EQ(after.chat->temperature, 0.3);
    EXPECT_DOUBLE_EQ(before.chat->temperature, 1.0) << "替换前取到的快照不变";
    EXPECT_NE(after.visionModel, before.visionModel);
    EXPECT_EQ(after.visionModel->model, "vision-next");
    EXPECT_TRUE(before.visionModel->model.empty()) << "替换前取到的快照不变";
    EXPECT_EQ(after.drawingModel, before.drawingModel) << "三个端点各自替换，没变的不动";
    EXPECT_EQ(after.workerModel, before.workerModel);
    EXPECT_EQ(after.voice, before.voice);
    EXPECT_EQ(after.memory, before.memory);
    EXPECT_EQ(after.webSearch, before.webSearch);
    EXPECT_EQ(after.webFetch, before.webFetch);
    EXPECT_EQ(after.dock, before.dock);
    EXPECT_EQ(after.glance, before.glance);
    EXPECT_EQ(after.message, before.message);
    EXPECT_EQ(summarizeReload(report),
              "配置已刷新：9 项变化，已生效 2 项。\n"
              "需重启才能生效 2 项：chat.worker_threads、storage.conversation_database。\n"
              "尚未接通实时生效、需重启 5 项：bot.group_chat_enabled、voice.enabled、voice.host、"
              "voice.port、network.proxy。");
}

// 端到端（L1.a）：真实的重载入口 + 产品规则表 + 持活值的 UserSessionService。
// 同时改一个接通的 chat 键与只能重启的 chat.worker_threads：模块下一次装配请求就用新温度，
// 重载前装配好的请求不变；反馈里已生效 1 项、需重启 1 项
TEST_F(ConfigReloaderFixture, ReloadedChatKeyReachesUserSessionOnNextRequest)
{
    const std::filesystem::path directory =
        path.parent_path() / (path.stem().string() + "-module");
    std::filesystem::create_directories(directory);
    const std::string registryPath = (directory / "models.json").string();
    std::ofstream(registryPath) << R"({"Models":[{"ModelName":["test-model"],"api_key":"k",)"
                                   R"("api_endpoint":"https://example.test/chat","APIStandard":"OpenAI"}]})";
    {
        ModelRegistry registry(registryPath);
        ConversationStore conversations((directory / "conversation.db").string());
        UserSessionService session(registry, conversations, BotIdentity{}, live->chat);
        ConfigReloader reloader(*store, *live);

        const std::optional<ChatCallBundle> before = session.buildChatRequest(10);
        ASSERT_TRUE(before.has_value());
        EXPECT_DOUBLE_EQ(before->request.temperature, 1.0);

        writeEdited([](nlohmann::json &config) {
            config["chat"]["temperature"] = 0.3;
            config["chat"]["worker_threads"] = 3;
        });
        const ConfigReloadReport report = reloader.reload();

        ASSERT_TRUE(report.result.success);
        const std::optional<ChatCallBundle> after = session.buildChatRequest(10);
        ASSERT_TRUE(after.has_value());
        EXPECT_DOUBLE_EQ(after->request.temperature, 0.3);
        EXPECT_DOUBLE_EQ(before->request.temperature, 1.0) << "重载前装配好的请求不受影响";
        EXPECT_EQ(summarizeReload(report),
                  "配置已刷新：2 项变化，已生效 1 项。\n"
                  "需重启才能生效 1 项：chat.worker_threads。");
    }
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

// 端到端（L1.b）：真实的重载入口 + 产品规则表 + 组合根同款的端点来源。
// 先调用一次，再改 models.vision.model 并重载：同一个 provider 下一次调用就按新名字解析，
// 之前拿到的结果不变；另外两个端点不受影响
TEST_F(ConfigReloaderFixture, ReloadedModelEndpointReachesProviderOnNextCall)
{
    const std::string registryPath =
        (path.parent_path() / (path.stem().string() + "-models.json")).string();
    std::ofstream(registryPath)
        << R"({"Models":[)"
           R"({"ModelName":["vision-a"],"api_key":"key-a","api_endpoint":"https://a.example.test","APIStandard":"OpenAI"},)"
           R"({"ModelName":["vision-b"],"api_key":"key-b","api_endpoint":"https://b.example.test","APIStandard":"Anthropic"}]})";
    {
        ModelRegistry registry(registryPath);
        const EndpointProvider vision = registryEndpoint(live->visionModel, registry);
        const EndpointProvider worker = registryEndpoint(live->workerModel, registry);
        ConfigReloader reloader(*store, *live);

        writeEdited([](nlohmann::json &config) { config["models"]["vision"] = {{"model", "vision-a"}}; });
        ASSERT_TRUE(reloader.reload().result.success);
        const ModelEndpointOptions before = vision();
        EXPECT_EQ(before.endpoint, "https://a.example.test");

        writeEdited([](nlohmann::json &config) { config["models"]["vision"] = {{"model", "vision-b"}}; });
        const ConfigReloadReport report = reloader.reload();

        ASSERT_TRUE(report.result.success);
        EXPECT_EQ(report.applied, (std::vector<std::string>{"models.vision.model"}));
        const ModelEndpointOptions after = vision();
        EXPECT_EQ(after.model, "vision-b");
        EXPECT_EQ(after.endpoint, "https://b.example.test");
        EXPECT_EQ(after.apiKey, "key-b");
        EXPECT_EQ(after.apiStandard, "Anthropic");
        EXPECT_EQ(before.endpoint, "https://a.example.test") << "之前拿到的结果不变";
        EXPECT_FALSE(worker().configured()) << "models.worker 没配，仍未配置";
        EXPECT_EQ(summarizeReload(report), "配置已刷新：1 项变化，已生效 1 项。");
    }
    std::error_code error;
    std::filesystem::remove(registryPath, error);
}

// 带应用函数的行：新快照发布之后换进活值，同一个应用函数只调一次；
// 没变化的行不应用，其他活值不动
TEST_F(ConfigReloaderFixture, AppliesChangedRowsOnceAfterPublishing)
{
    ConfigReloader reloader(*store, *live, ruleTable(kApplyingRules));
    writeEdited([](nlohmann::json &config) {
        config["chat"]["temperature"] = 0.3;
        config["chat"]["top_p"] = 0.8;
        config["chat"]["worker_threads"] = 3;
        config["memory"] = {{"batch_turns", 4}};
    });
    const LiveSnapshots before = snapshotsOf(*live);

    const ConfigReloadReport report = reloader.reload();

    ASSERT_TRUE(report.result.success);
    EXPECT_EQ(report.applied, (std::vector<std::string>{"chat.temperature", "chat.top_p"}));
    EXPECT_EQ(report.restartOnly, (std::vector<std::string>{"chat.worker_threads"}));
    EXPECT_EQ(report.notWired, (std::vector<std::string>{"memory.batch_turns"}));
    EXPECT_EQ(chatApplyCalls.load(), 1) << "同一个应用函数一次重载只调一次";
    EXPECT_EQ(voiceApplyCalls.load(), 0) << "没变化的行不应用";

    const Live<ChatOptions>::Snapshot chat = live->chat.get();
    EXPECT_NE(chat, before.chat);
    EXPECT_DOUBLE_EQ(chat->temperature, 0.3);
    EXPECT_DOUBLE_EQ(chat->topP, 0.8);
    EXPECT_DOUBLE_EQ(chat->temperature, store->current()->runtime.chat.temperature);
    EXPECT_DOUBLE_EQ(before.chat->temperature, 1.0) << "替换前取到的快照不变";
    EXPECT_EQ(live->voice.get(), before.voice);
    EXPECT_EQ(live->memory.get(), before.memory);
    EXPECT_EQ(live->message.get(), before.message);
    EXPECT_EQ(summarizeReload(report),
              "配置已刷新：4 项变化，已生效 2 项。\n"
              "需重启才能生效 1 项：chat.worker_threads。\n"
              "尚未接通实时生效、需重启 1 项：memory.batch_turns。");

    // 文件没再变：第二次重载没有变化，不应用，活值不动
    const LiveSnapshots afterFirst = snapshotsOf(*live);
    const ConfigReloadReport again = reloader.reload();
    ASSERT_TRUE(again.result.success);
    EXPECT_TRUE(again.applied.empty());
    EXPECT_EQ(chatApplyCalls.load(), 1);
    expectAllUnchanged(afterFirst, *live);
    EXPECT_EQ(summarizeReload(again), "配置校验通过，内存快照无变化。");
}

// 并发的重载排队执行：发布与应用不会交错
TEST_F(ConfigReloaderFixture, ConcurrentReloadsAreSerialized)
{
    slowInside = 0;
    slowOverlaps = 0;
    slowCalls = 0;
    ConfigReloader reloader(*store, *live, ruleTable(kSlowRules));
    std::atomic<int> failures{0};

    std::vector<std::thread> threads;
    for (int thread = 0; thread < 4; ++thread)
    {
        threads.emplace_back([&reloader, &failures]() {
            for (int round = 0; round < 5; ++round)
            {
                if (!reloader.reload().result.success)
                    failures.fetch_add(1);
            }
        });
    }
    for (std::thread &thread : threads)
        thread.join();

    EXPECT_EQ(failures.load(), 0);
    EXPECT_EQ(slowCalls.load(), 20);
    EXPECT_EQ(slowOverlaps.load(), 0);
}
