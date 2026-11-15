#include <gtest/gtest.h>

#include "Action/AdminControlAction.h"
#include "Action/VoiceModeAction.h"
#include "Bootstrap/ConfigReloader.h"
#include "Bootstrap/ConfigRules.h"
#include "Bootstrap/ConfigSnapshotStore.h"
#include "Bootstrap/LiveSettings.h"
#include "ComputerStatus/ComputerStatus.h"
#include "Configuration/ConfigLoader.h"
#include "Memory/MemoryService.h"
#include "ModelApiCaller/Dock.hpp"
#include "ModelApiCaller/ModelEndpointResolver.h"
#include "ModelRegistry/ModelRegistry.h"
#include "Persistence/ConversationStore.h"
#include "UserSession/UserSessionService.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
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
    {"chat.presence_penalty", applies(applyChat),               field<&SchemaConfig::chat, &ChatSchema::presencePenalty>},
    {"voice.host",          applies(applyVoice),                field<&SchemaConfig::voice, &VoiceSchema::host>},
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

// 记下每次记忆提取请求用的模型名（后台线程调用，带锁），可等到第 N 次请求
class ExtractionLog
{
public:
    void record(const std::string &modelName)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            names.push_back(modelName);
        }
        changed.notify_all();
    }

    std::vector<std::string> waitFor(std::size_t count)
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait_for(lock, std::chrono::seconds(5), [&]() { return names.size() >= count; });
        return names;
    }

private:
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> names;
};

class ExtractionRecordingPort final : public LLMPort
{
public:
    explicit ExtractionRecordingPort(ExtractionLog *log) : log(log) {}
    ChatResponse request_chat(const ChatModel &, const std::string &modelName, const ChatRequest &) override
    {
        log->record(modelName);
        ChatResponse response;
        response.code = 200;
        response.content = R"({"memories":[]})";
        response.finish_reason = "stop";
        return response;
    }
    VisionResponse request_vision(const ChatModel &, const std::string &, const std::string &,
                                  const std::string &) override
    {
        return {};
    }
    ImageResponse request_image(const ChatModel &, const std::string &, const std::string &) override
    {
        return {};
    }

private:
    ExtractionLog *log;
};

// 全部活值的快照：持有 shared_ptr，旧快照不会被释放，指针比较不会撞上复用的地址
struct LiveSnapshots
{
    Live<BotIdentity>::Snapshot bot;
    Live<ChatOptions>::Snapshot chat;
    Live<ModelEndpointOptions>::Snapshot drawingModel;
    Live<ModelEndpointOptions>::Snapshot visionModel;
    Live<ModelEndpointOptions>::Snapshot workerModel;
    Live<VoiceOptions>::Snapshot voice;
    Live<bool>::Snapshot globalVoice;
    Live<MemoryOptions>::Snapshot memory;
    Live<WebSearchOptions>::Snapshot webSearch;
    Live<WebFetchOptions>::Snapshot webFetch;
    Live<DockOptions>::Snapshot dock;
    Live<GlanceOptions>::Snapshot glance;
    Live<MessageOptions>::Snapshot message;
    Live<AffectOptions>::Snapshot affect;
};

LiveSnapshots snapshotsOf(const LiveSettings &live)
{
    return {live.bot.get(),
            live.chat.get(),         live.drawingModel.get(), live.visionModel.get(),
            live.workerModel.get(),  live.voice.get(),        live.globalVoice.get(),
            live.memory.get(),       live.webSearch.get(),    live.webFetch.get(),
            live.dock.get(),         live.glance.get(),       live.message.get(),
            live.affect.get()};
}

void expectAllUnchanged(const LiveSnapshots &before, const LiveSettings &live)
{
    const LiveSnapshots now = snapshotsOf(live);
    EXPECT_EQ(now.bot, before.bot);
    EXPECT_EQ(now.chat, before.chat);
    EXPECT_EQ(now.drawingModel, before.drawingModel);
    EXPECT_EQ(now.visionModel, before.visionModel);
    EXPECT_EQ(now.workerModel, before.workerModel);
    EXPECT_EQ(now.voice, before.voice);
    EXPECT_EQ(now.globalVoice, before.globalVoice);
    EXPECT_EQ(now.memory, before.memory);
    EXPECT_EQ(now.webSearch, before.webSearch);
    EXPECT_EQ(now.webFetch, before.webFetch);
    EXPECT_EQ(now.dock, before.dock);
    EXPECT_EQ(now.glance, before.glance);
    EXPECT_EQ(now.message, before.message);
    EXPECT_EQ(now.affect, before.affect);
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
            EXPECT_EQ(summarizeReload(report), "配置刷新失败，继续使用当前内存快照。") << bad.name;
    }
}

// 产品规则表（默认）：只有接通的行把新值换进活值（L1.a 起 chat 组 6 个键，L1.b 起
// models 的 12 个键，L1.c 起 voice 的 5 个键，L1.d 起 memory 除 enabled 外的 4 个键，
// L1.e 起搜索、抓取参数与 network.proxy，L1.f 起群聊开关、affect.enforce、瞥视预算），
// 其余活值的快照指针不变；只能重启的字段在活值里
// 保持运行值（决定 9 的同一做法）；
// 反馈如实列出已生效与需重启的键
TEST_F(ConfigReloaderFixture, ProductRulesApplyOnlyWiredRows)
{
    ConfigReloader reloader(*store, *live);
    writeEdited([](nlohmann::json &config) {
        config["bot"]["group_chat_enabled"] = false;
        config["chat"]["temperature"] = 0.3;
        config["chat"]["worker_threads"] = 3;
        config["models"]["vision"] = {{"model", "vision-next"}};
        config["voice"] = {{"enabled", true}, {"host", "127.0.0.1"}, {"port", "9880"}};
        // 其余 memory 键写成与缺省节相同的值，只让 enabled 与 recall_limit 变化
        config["memory"] = {{"enabled", false}, {"batch_turns", 10}, {"idle_minutes", 30},
                            {"recall_limit", 5}};
        config["storage"] = {{"conversation_database", "next.db"}};
        config["network"] = {{"proxy", "http://127.0.0.1:7890"}};
    });
    const LiveSnapshots before = snapshotsOf(*live);

    const ConfigReloadReport report = reloader.reload();

    ASSERT_TRUE(report.result.success);
    EXPECT_EQ(report.result.diff.size(), 11U);
    EXPECT_EQ(store->current(), report.result.snapshot);
    EXPECT_EQ(report.applied,
              (std::vector<std::string>{"bot.group_chat_enabled", "chat.temperature",
                                        "models.vision.model", "voice.enabled", "voice.host",
                                        "voice.port", "memory.enabled", "memory.recall_limit",
                                        "network.proxy"}));
    EXPECT_EQ(report.restartOnly,
              (std::vector<std::string>{"chat.worker_threads", "storage.conversation_database"}));

    const LiveSnapshots after = snapshotsOf(*live);
    EXPECT_NE(after.chat, before.chat);
    EXPECT_DOUBLE_EQ(after.chat->temperature, 0.3);
    EXPECT_DOUBLE_EQ(before.chat->temperature, 1.0) << "替换前取到的快照不变";
    EXPECT_NE(after.visionModel, before.visionModel);
    EXPECT_EQ(after.visionModel->model, "vision-next");
    EXPECT_TRUE(before.visionModel->model.empty()) << "替换前取到的快照不变";
    EXPECT_EQ(after.drawingModel, before.drawingModel) << "三个端点各自替换，没变的不动";
    EXPECT_EQ(after.workerModel, before.workerModel);
    EXPECT_NE(after.voice, before.voice);
    EXPECT_EQ(after.voice->host, "127.0.0.1");
    EXPECT_TRUE(*after.globalVoice) << "voice.enabled 变了：总开关置为新值";
    EXPECT_FALSE(*before.globalVoice);
    EXPECT_NE(after.memory, before.memory);
    EXPECT_EQ(after.memory->recallLimit, 5U);
    EXPECT_FALSE(after.memory->enabled) << "memory.enabled 自 L2.b 起接通";
    EXPECT_EQ(after.dock->proxy, "http://127.0.0.1:7890") << "network.proxy 一个键换三个活值";
    EXPECT_EQ(after.webSearch->proxy, "http://127.0.0.1:7890");
    EXPECT_EQ(after.webFetch->proxy, "http://127.0.0.1:7890");
    EXPECT_TRUE(before.dock->proxy.empty()) << "替换前取到的快照不变";
    EXPECT_EQ(after.glance, before.glance);
    EXPECT_NE(after.message, before.message);
    EXPECT_FALSE(after.message->groupChatEnabled);
    EXPECT_EQ(after.message->bot.id, before.message->bot.id) << "bot 身份保持运行值（决定 9）";
    EXPECT_EQ(after.affect, before.affect);
    EXPECT_EQ(summarizeReload(report),
              "配置已刷新：11 项变化，已生效 9 项。\n"
              "需重启才能生效 2 项：chat.worker_threads、storage.conversation_database。");
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
        UserSessionService session(registry, conversations, live->bot, live->chat);
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

// 端到端（L1.c）：语音总开关跟着 voice.enabled 的变化走，管理员命令也能改，后写者生效；
// 只改合成参数时总开关不动。读开关的 VoiceModeAction 每次执行时现取
TEST_F(ConfigReloaderFixture, VoiceSwitchFollowsConfigChangesAndCommandsLastWriterWins)
{
    const std::filesystem::path directory =
        path.parent_path() / (path.stem().string() + "-voice");
    std::filesystem::create_directories(directory);
    const std::string registryPath = (directory / "models.json").string();
    std::ofstream(registryPath) << R"({"Models":[{"ModelName":["test-model"],"api_key":"k",)"
                                   R"("api_endpoint":"https://example.test/chat","APIStandard":"OpenAI"}]})";
    {
        ModelRegistry registry(registryPath);
        ConversationStore conversations((directory / "conversation.db").string());
        UserSessionService session(registry, conversations, live->bot, live->chat);
        ComputerStatus computerStatus;
        VoiceModeAction voiceMode(session, live->globalVoice);
        AdminControlAction admin(computerStatus, live->globalVoice, [] { return std::string(); });
        ConfigReloader reloader(*store, *live);
        const nlohmann::json turnOn = {{"enabled", true}};
        const nlohmann::json freeze = {{"action", "freeze_global_voice"}};
        const nlohmann::json activate = {{"action", "activate_global_voice"}};
        ActionContext user;
        user.user_id = 10;
        const auto userTurnsVoiceOn = [&]() { return voiceMode.execute(turnOn, user).content; };
        const auto writeVoice = [this](bool enabled, const std::string &host) {
            writeEdited([enabled, host](nlohmann::json &config) {
                config["voice"] = {{"enabled", enabled}, {"host", host}, {"port", "9880"}};
            });
        };

        // 启动时没配 voice：总开关关着，用户开不了语音
        EXPECT_EQ(userTurnsVoiceOn(), "管理员尚未激活语音功能。");

        // 配置打开语音：重载后总开关打开，下一次执行即放行
        writeVoice(true, "127.0.0.1");
        ConfigReloadReport report = reloader.reload();
        ASSERT_TRUE(report.result.success);
        EXPECT_EQ(report.applied,
                  (std::vector<std::string>{"voice.enabled", "voice.host", "voice.port"}));
        EXPECT_EQ(userTurnsVoiceOn(), "语音已开启。");
        EXPECT_TRUE(session.isVoiceMode(10));

        // 管理员冻结：命令后写，开关关上
        EXPECT_EQ(admin.execute(freeze, ActionContext{}).content, "全局语音已冻结。");
        EXPECT_EQ(userTurnsVoiceOn(), "管理员尚未激活语音功能。");

        // 只改合成参数：voice.enabled 没变，总开关保持命令设的值
        writeVoice(true, "127.0.0.2");
        report = reloader.reload();
        ASSERT_TRUE(report.result.success);
        EXPECT_EQ(report.applied, (std::vector<std::string>{"voice.host"}));
        EXPECT_EQ(live->voice.get()->host, "127.0.0.2");
        EXPECT_FALSE(*live->globalVoice.get());
        EXPECT_EQ(userTurnsVoiceOn(), "管理员尚未激活语音功能。");

        // 管理员再激活，之后配置把 voice.enabled 改成 false：配置后写，开关关上
        EXPECT_EQ(admin.execute(activate, ActionContext{}).content, "全局语音已激活。");
        EXPECT_EQ(userTurnsVoiceOn(), "语音已开启。");
        writeVoice(false, "127.0.0.2");
        report = reloader.reload();
        ASSERT_TRUE(report.result.success);
        EXPECT_EQ(report.applied, (std::vector<std::string>{"voice.enabled"}));
        EXPECT_EQ(userTurnsVoiceOn(), "管理员尚未激活语音功能。");
    }
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

// 端到端（L1.d）：没写 memory.model 时，加载阶段取 chat.default_model 的值。改默认模型后，
// 差异里 memory.model 跟着变、一并应用，记忆服务下一次提取就用新模型；显式写了
// memory.model 的不跟随。第二次重载同时把 batch_turns 从 2 降到 1：重载前那一轮手里的
// 旧快照挑不中新入队的单轮批次，提取只能出自新快照
TEST_F(ConfigReloaderFixture, MemoryModelFollowsDefaultModelWhenNotConfigured)
{
    const std::filesystem::path directory =
        path.parent_path() / (path.stem().string() + "-memory");
    std::filesystem::create_directories(directory);
    const std::string registryPath = (directory / "models.json").string();
    std::ofstream(registryPath) << R"({"Models":[{"ModelName":["test-model","next-model"],"api_key":"k",)"
                                   R"("api_endpoint":"https://example.test/chat","APIStandard":"Fake"}]})";
    {
        ModelRegistry registry(registryPath);
        const std::string databasePath = (directory / "conversation.db").string();
        ConversationStore conversations(databasePath);
        std::atomic<bool> running{true};
        Dock dock(live->dock, &running);
        ExtractionLog log;
        dock.registerStandard("Fake", std::make_unique<ExtractionRecordingPort>(&log));
        ConfigReloader reloader(*store, *live);

        // memory 节里不写 model
        writeEdited([](nlohmann::json &config) { config["memory"] = {{"batch_turns", 2}}; });
        ASSERT_TRUE(reloader.reload().result.success);
        EXPECT_EQ(live->memory.get()->model, "test-model");
        MemoryService memory(databasePath, conversations, dock, registry, live->memory);
        ASSERT_TRUE(memory.isEnabled());
        memory.enqueueTurn(10, "我喜欢塞尔达", "记住了", 1, 2);
        memory.enqueueTurn(10, "我住在上海", "好的", 3, 4);
        EXPECT_EQ(log.waitFor(1), (std::vector<std::string>{"test-model"}));

        writeEdited([](nlohmann::json &config) {
            config["chat"]["default_model"] = "next-model";
            config["memory"] = {{"batch_turns", 1}};
        });
        const ConfigReloadReport report = reloader.reload();
        ASSERT_TRUE(report.result.success);
        EXPECT_EQ(report.applied, (std::vector<std::string>{"chat.default_model", "memory.model",
                                                            "memory.batch_turns"}));
        memory.enqueueTurn(11, "我养了一只猫", "真好", 5, 6);
        EXPECT_EQ(log.waitFor(2), (std::vector<std::string>{"test-model", "next-model"}));

        // 显式写了 memory.model：默认模型再变，它不跟
        writeEdited([](nlohmann::json &config) {
            config["chat"]["default_model"] = "test-model";
            config["memory"] = {{"batch_turns", 1}, {"model", "next-model"}};
        });
        const ConfigReloadReport pinned = reloader.reload();
        ASSERT_TRUE(pinned.result.success);
        EXPECT_EQ(pinned.applied, (std::vector<std::string>{"chat.default_model"}));
        EXPECT_EQ(live->memory.get()->model, "next-model");
        memory.shutdown();
    }
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

// network.proxy（L1.e）：一个键作用于模型调用、搜索、抓取三处，应用函数同时换三个活值，
// 每个只换 proxy；搜索与抓取的 enabled（L2.a 起接通）跟着各自的组换成新值
TEST_F(ConfigReloaderFixture, ProxyChangeReachesModelSearchAndFetchTogether)
{
    ConfigReloader reloader(*store, *live);
    const auto writeWeb = [this](const std::string &proxy, bool searchEnabled) {
        writeEdited([proxy, searchEnabled](nlohmann::json &config) {
            config["web_search"] = {{"enabled", searchEnabled},
                                    {"api_key", {{"literal", "search-key"}}},
                                    {"max_results", 7}};
            config["web_fetch"] = {{"enabled", true}};
            if (!proxy.empty())
                config["network"] = {{"proxy", proxy}};
        });
    };

    // 先把搜索、抓取的参数与开关换成新值（代理还没配）
    writeWeb("", true);
    ASSERT_TRUE(reloader.reload().result.success);
    EXPECT_EQ(live->webSearch.get()->maxResults, 7U);
    EXPECT_TRUE(live->webSearch.get()->enabled);
    EXPECT_TRUE(live->webFetch.get()->enabled);
    const LiveSnapshots before = snapshotsOf(*live);

    // 配上代理：三个活值一起换，其余字段不动
    writeWeb("http://127.0.0.1:7890", true);
    ConfigReloadReport report = reloader.reload();
    ASSERT_TRUE(report.result.success);
    EXPECT_EQ(report.applied, (std::vector<std::string>{"network.proxy"}));
    const LiveSnapshots after = snapshotsOf(*live);
    EXPECT_EQ(after.dock->proxy, "http://127.0.0.1:7890");
    EXPECT_EQ(after.webSearch->proxy, "http://127.0.0.1:7890");
    EXPECT_EQ(after.webFetch->proxy, "http://127.0.0.1:7890");
    EXPECT_EQ(after.webSearch->maxResults, 7U);
    EXPECT_EQ(after.webSearch->apiKey, "search-key");
    EXPECT_TRUE(after.webSearch->enabled);
    EXPECT_TRUE(after.webFetch->enabled);
    EXPECT_TRUE(before.webSearch->proxy.empty()) << "替换前取到的快照不变";
    EXPECT_EQ(after.chat, before.chat);
    EXPECT_EQ(after.memory, before.memory);

    // 去掉代理、同时关掉搜索：代理三处一起清空，搜索开关关上（L2.a：工具随之不开放）。
    // 加载器只在搜索开着时读 api_key，关掉后 api_key 读成空，与开关一起换进活值
    writeWeb("", false);
    report = reloader.reload();
    ASSERT_TRUE(report.result.success);
    EXPECT_EQ(report.applied,
              (std::vector<std::string>{"web_search.enabled", "web_search.api_key", "network.proxy"}));
    EXPECT_FALSE(live->webSearch.get()->enabled);
    EXPECT_TRUE(live->webSearch.get()->apiKey.empty());
    EXPECT_TRUE(live->webFetch.get()->enabled);
    EXPECT_TRUE(live->dock.get()->proxy.empty());
    EXPECT_TRUE(live->webSearch.get()->proxy.empty());
    EXPECT_TRUE(live->webFetch.get()->proxy.empty());
    EXPECT_EQ(live->webSearch.get()->maxResults, 7U);
}

// 端到端（L1.f 起）：群聊开关、affect.enforce、瞥视预算接通应用；glance.enabled 自 L2.b、
// affect 其余三键自 L2.c、bot.name 自 L2.d 起接通；bot.id 等只能重启的字段保持运行值（决定 9）
TEST_F(ConfigReloaderFixture, GroupChatEnforceAndGlanceBudgetApplyKeepingRunningFields)
{
    ConfigReloader reloader(*store, *live);
    const LiveSnapshots before = snapshotsOf(*live);
    writeEdited([](nlohmann::json &config) {
        config["bot"]["group_chat_enabled"] = false;
        config["bot"]["name"] = "Alice";
        config["affect"] = {{"enabled", true}, {"policy", "judge"}, {"enforce", true}};
        config["glance"] = {{"enabled", true}, {"per_group_per_minute", 12}};
    });

    const ConfigReloadReport report = reloader.reload();

    ASSERT_TRUE(report.result.success);
    EXPECT_EQ(report.applied,
              (std::vector<std::string>{"bot.name", "bot.group_chat_enabled", "affect.enabled",
                                        "affect.policy", "affect.enforce", "glance.enabled",
                                        "glance.per_group_per_minute"}));
    const LiveSnapshots after = snapshotsOf(*live);
    EXPECT_FALSE(after.message->groupChatEnabled);
    EXPECT_EQ(after.message->bot.name, "Alice") << "bot.name 自 L2.d 起接通，MessageOptions 里同步";
    EXPECT_EQ(after.message->bot.id, 10001U) << "id 只能重启，保持运行值（决定 9）";
    EXPECT_EQ(after.bot->name, "Alice");
    EXPECT_EQ(after.bot->id, 10001U);
    EXPECT_EQ(after.bot->managerId, 10002U);
    EXPECT_TRUE(after.affect->enforce);
    EXPECT_TRUE(after.affect->enabled) << "affect.enabled、policy 自 L2.c 起接通";
    EXPECT_EQ(after.affect->policy, "judge");
    EXPECT_EQ(after.glance->perGroupPerMinute, 12);
    EXPECT_TRUE(after.glance->enabled) << "glance.enabled 自 L2.b 起接通";
    EXPECT_TRUE(before.message->groupChatEnabled) << "替换前取到的快照不变";
    EXPECT_FALSE(before.affect->enforce);
    EXPECT_EQ(after.chat, before.chat);
}

// 带应用函数的行：新快照发布之后换进活值，同一个应用函数只调一次；
// 没变化的行不应用，其他活值不动
TEST_F(ConfigReloaderFixture, AppliesChangedRowsOnceAfterPublishing)
{
    ConfigReloader reloader(*store, *live, ruleTable(kApplyingRules));
    writeEdited([](nlohmann::json &config) {
        config["chat"]["temperature"] = 0.3;
        config["chat"]["presence_penalty"] = 0.8;
        config["chat"]["worker_threads"] = 3;
    });
    const LiveSnapshots before = snapshotsOf(*live);

    const ConfigReloadReport report = reloader.reload();

    ASSERT_TRUE(report.result.success);
    EXPECT_EQ(report.applied, (std::vector<std::string>{"chat.temperature", "chat.presence_penalty"}));
    EXPECT_EQ(report.restartOnly, (std::vector<std::string>{"chat.worker_threads"}));
    EXPECT_EQ(chatApplyCalls.load(), 1) << "同一个应用函数一次重载只调一次";
    EXPECT_EQ(voiceApplyCalls.load(), 0) << "没变化的行不应用";

    const Live<ChatOptions>::Snapshot chat = live->chat.get();
    EXPECT_NE(chat, before.chat);
    EXPECT_DOUBLE_EQ(chat->temperature, 0.3);
    EXPECT_DOUBLE_EQ(chat->presencePenalty, 0.8);
    EXPECT_DOUBLE_EQ(chat->temperature, store->current()->runtime.chat.temperature);
    EXPECT_DOUBLE_EQ(before.chat->temperature, 1.0) << "替换前取到的快照不变";
    EXPECT_EQ(live->voice.get(), before.voice);
    EXPECT_EQ(live->memory.get(), before.memory);
    EXPECT_EQ(live->message.get(), before.message);
    EXPECT_EQ(summarizeReload(report),
              "配置已刷新：3 项变化，已生效 2 项。\n"
              "需重启才能生效 1 项：chat.worker_threads。");

    // 文件没再变：第二次重载没有变化，不应用，活值不动
    const LiveSnapshots afterFirst = snapshotsOf(*live);
    const ConfigReloadReport again = reloader.reload();
    ASSERT_TRUE(again.result.success);
    EXPECT_TRUE(again.applied.empty());
    EXPECT_EQ(chatApplyCalls.load(), 1);
    expectAllUnchanged(afterFirst, *live);
    EXPECT_EQ(summarizeReload(again), "配置校验通过，内存快照无变化。");
}

// chat.top_p 已删除（2026-11-15）。线上旧配置里还写着它：启动照常（只有一条未知字段告警，
// 没有 Error/Fatal），重载照常发布、照常应用别的键，删掉的键不进差异、不进反馈
namespace
{
bool hasUnknownFieldWarning(const std::vector<ConfigDiagnostic> &diagnostics, const std::string &path)
{
    for (const ConfigDiagnostic &diagnostic : diagnostics)
    {
        if (diagnostic.path == path && diagnostic.severity == ConfigSeverity::Warning &&
            diagnostic.category == ConfigErrorCategory::Unknown)
            return true;
    }
    return false;
}
}

TEST_F(ConfigReloaderFixture, LegacyTopPOnlyWarnsAtStartupAndReload)
{
    nlohmann::json legacy = nlohmann::json::parse(kBaseConfig);
    legacy["chat"]["top_p"] = 0.9;

    // 启动：能启动（canStart 即没有 Error/Fatal），top_p 只是一条未知字段告警
    const ConfigLoadResult startup = ConfigLoader().loadText(legacy.dump());
    ASSERT_TRUE(startup.canStart());
    EXPECT_TRUE(hasUnknownFieldWarning(startup.diagnostics, "chat.top_p"));

    // 重载：文件里新加上 top_p 并改了温度，重载成功，温度照常生效
    ConfigReloader reloader(*store, *live);
    writeEdited([](nlohmann::json &config) {
        config["chat"]["top_p"] = 0.9;
        config["chat"]["temperature"] = 0.4;
    });
    ConfigReloadReport report = reloader.reload();
    ASSERT_TRUE(report.result.success);
    EXPECT_EQ(store->current(), report.result.snapshot);
    EXPECT_TRUE(hasUnknownFieldWarning(report.result.diagnostics, "chat.top_p"));
    EXPECT_EQ(report.applied, (std::vector<std::string>{"chat.temperature"}));
    EXPECT_TRUE(report.restartOnly.empty());
    EXPECT_DOUBLE_EQ(live->chat.get()->temperature, 0.4);
    EXPECT_EQ(summarizeReload(report), "配置已刷新：1 项变化，已生效 1 项。");

    // 只改 top_p 的值：没有任何变化
    writeEdited([](nlohmann::json &config) {
        config["chat"]["top_p"] = 0.5;
        config["chat"]["temperature"] = 0.4;
    });
    report = reloader.reload();
    ASSERT_TRUE(report.result.success);
    EXPECT_TRUE(report.result.diff.empty());
    EXPECT_TRUE(hasUnknownFieldWarning(report.result.diagnostics, "chat.top_p"));
    EXPECT_EQ(summarizeReload(report), "配置校验通过，内存快照无变化。");
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
