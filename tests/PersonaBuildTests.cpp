#include <gtest/gtest.h>

#include "Persistence/ConversationStore.h"
#include "UserSession/UserSessionService.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
std::string writeModelRegistryFile(const std::filesystem::path &directory)
{
    const auto path = directory / "models.json";
    {
        std::ofstream output(path);
        output << R"({
            "Models": [
                {"ModelName": ["test-model"], "api_key": "key",
                 "api_endpoint": "https://example.test/chat", "APIStandard": "OpenAI"}
            ]
        })";
    }
    return path.string();
}

std::string writeFile(const std::filesystem::path &path, const std::string &content)
{
    {
        std::ofstream output(path);
        output << content;
    }
    return path.string();
}

const char *kSampleSoul = "克莱茵：理性、高效、偶尔吐槽的AI助手。";
}

TEST(PersonaBuildLifecycleTest, ColdStartAndResetArmBuildAndApplyCompiledPrompt)
{
    const auto dir = std::filesystem::temp_directory_path() /
                     ("kleinbot-persona-" + std::to_string(::time(nullptr)));
    std::filesystem::create_directories(dir);
    writeModelRegistryFile(dir);
    writeFile(dir / "soul.md", kSampleSoul);

    ConversationStore store((dir / "conversation.db").string());
    ModelRegistry registry(writeModelRegistryFile(dir));
    ChatOptions options;
    options.defaultModel = "test-model";
    BotIdentity bot;
    UserSessionService session(registry, store, bot, options,
                               (dir / "soul.md").string());

    // 冷启动（首次出现）即置位：新对话周期重新编译人格
    session.ensureUserExists(10);
    EXPECT_TRUE(session.personaBuildPending(10));

    // 冷启动（新用户首次出现）同置位
    session.ensureUserExists(20);
    EXPECT_TRUE(session.personaBuildPending(20));

    session.resetChat(10);
    EXPECT_TRUE(session.personaBuildPending(10));

    // 编译任务可用（规范内嵌，无失败路径），素材含 soul 内容
    std::string compileSystem;
    std::string compileTask;
    session.personaBuildTask(compileSystem, compileTask);
    EXPECT_NE(compileSystem.find("人格编译规范"), std::string::npos);
    EXPECT_NE(compileTask.find("克莱茵"), std::string::npos);

    // 编译失败（空产物）：标志保留，下轮重试
    session.finishPersonaBuild(10, "");
    EXPECT_TRUE(session.personaBuildPending(10));
    EXPECT_NE(session.getUserConfig(10).system_prompt.find("克莱茵"), std::string::npos)
        << "失败时维持 soul.md 兜底";

    // 成功应用：prompt 替换、标志清除
    session.finishPersonaBuild(10, "CHARACTER_KLEIN\nSTYLE_BRIEF");
    EXPECT_FALSE(session.personaBuildPending(10));
    EXPECT_EQ(session.getUserConfig(10).system_prompt, "CHARACTER_KLEIN\nSTYLE_BRIEF");

    // 彻底重置：清掉编译产物回归源码，重新置位编译标志
    session.resetContext(10);
    EXPECT_TRUE(session.personaBuildPending(10));
    EXPECT_NE(session.getUserConfig(10).system_prompt.find("克莱茵"), std::string::npos)
        << "旧编译产物应已被 soul.md 兜底替换";

    std::filesystem::remove_all(dir);
}

TEST(PersonaBuildLifecycleTest, ManualPersonaSuppressesBuild)
{
    const auto dir = std::filesystem::temp_directory_path() /
                     ("kleinbot-persona-b-" + std::to_string(::time(nullptr)));
    std::filesystem::create_directories(dir);
    writeModelRegistryFile(dir);
    writeFile(dir / "soul.md", kSampleSoul);

    ConversationStore store((dir / "conversation.db").string());
    ModelRegistry registry(writeModelRegistryFile(dir));
    ChatOptions options;
    options.defaultModel = "test-model";
    BotIdentity bot;
    UserSessionService session(registry, store, bot, options,
                               (dir / "soul.md").string());

    session.ensureUserExists(10);
    session.setPersonality(10, "手动人格优先");
    session.resetChat(10);
    // 手动人格是部署者明确意志：重置不触发编译
    EXPECT_FALSE(session.personaBuildPending(10));
    // 彻底重置同理：手动人格保留，不编译
    session.resetContext(10);
    EXPECT_FALSE(session.personaBuildPending(10));
    EXPECT_EQ(session.getUserConfig(10).system_prompt, "手动人格优先");

    std::filesystem::remove_all(dir);
}

TEST(PersonaSharedCacheTest, BuiltPromptIsSharedAndInvalidatedByFileChange)
{
    const auto dir = std::filesystem::temp_directory_path() /
                     ("kleinbot-persona-share-" + std::to_string(::time(nullptr)));
    std::filesystem::create_directories(dir);
    writeModelRegistryFile(dir);
    writeFile(dir / "soul.md", kSampleSoul);

    ConversationStore store((dir / "conversation.db").string());
    ModelRegistry registry(writeModelRegistryFile(dir));
    ChatOptions options;
    options.defaultModel = "test-model";
    BotIdentity bot;
    UserSessionService session(registry, store, bot, options,
                               (dir / "soul.md").string());

    // 用户 A 完成一次编译：产物进入共享缓存
    session.ensureUserExists(10);
    session.resetChat(10);
    ASSERT_TRUE(session.tryBeginPersonaBuild());
    session.finishPersonaBuild(10, "COMPILED_V1");
    EXPECT_FALSE(session.personaBuildPending(10));
    EXPECT_EQ(session.getUserConfig(10).system_prompt, "COMPILED_V1");

    // 新用户 B（待编译）→ 缓存命中，零 LLM 直接复用
    session.ensureUserExists(20);
    session.resetChat(20);
    ASSERT_TRUE(session.personaBuildPending(20));
    auto shared = session.freshSharedPersona();
    ASSERT_TRUE(shared.has_value());
    EXPECT_EQ(*shared, "COMPILED_V1");
    session.applyGeneratedPersona(20, *shared);
    EXPECT_EQ(session.getUserConfig(20).system_prompt, "COMPILED_V1");
    EXPECT_FALSE(session.personaBuildPending(20));

    // soul.md 变更 → 哈希失效 → 缓存不再命中，需要重编；已应用的用户不受影响
    writeFile(dir / "soul.md", "克莱茵 v2：性格大改。");
    EXPECT_FALSE(session.freshSharedPersona().has_value());
    EXPECT_FALSE(session.personaBuildPending(10));

    // 单飞：获权期间他人 tryBegin 失败；空产物释放单飞但保留标志（重试语义）
    session.ensureUserExists(30);
    session.resetChat(30);
    ASSERT_TRUE(session.tryBeginPersonaBuild());
    EXPECT_FALSE(session.tryBeginPersonaBuild());
    session.finishPersonaBuild(30, "");
    EXPECT_TRUE(session.personaBuildPending(30));
    ASSERT_TRUE(session.tryBeginPersonaBuild());
    session.finishPersonaBuild(30, "COMPILED_V2");

    // 新哈希发布后缓存指向 v2
    EXPECT_FALSE(session.personaBuildPending(30));
    auto sharedV2 = session.freshSharedPersona();
    ASSERT_TRUE(sharedV2.has_value());
    EXPECT_EQ(*sharedV2, "COMPILED_V2");

    std::filesystem::remove_all(dir);
}
