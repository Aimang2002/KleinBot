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

const char *kSampleSpec = "# 人格编译规范\n标签块只含标签，禁止职责条款。\n";
const char *kSampleSoul = "克莱茵：理性、高效、偶尔吐槽的AI助手。";
}

TEST(PersonaBuildLifecycleTest, ResetSetsFlagAndBuildAppliesCompiledPrompt)
{
    const auto dir = std::filesystem::temp_directory_path() /
                     ("kleinbot-persona-" + std::to_string(::time(nullptr)));
    std::filesystem::create_directories(dir);
    writeModelRegistryFile(dir);
    writeFile(dir / "soul.md", kSampleSoul);
    writeFile(dir / "persona-spec.md", kSampleSpec);

    ConversationStore store((dir / "conversation.db").string());
    ModelRegistry registry(writeModelRegistryFile(dir));
    ChatOptions options;
    options.defaultModel = "test-model";
    BotIdentity bot;
    UserSessionService session(registry, store, bot, options,
                               (dir / "soul.md").string(),
                               (dir / "persona-spec.md").string());

    // 冷启动（首次出现）即置位：新对话周期重新编译人格
    session.ensureUserExists(10);
    EXPECT_TRUE(session.personaBuildPending(10));

    // 冷启动（新用户首次出现）同置位
    session.ensureUserExists(20);
    EXPECT_TRUE(session.personaBuildPending(20));

    session.resetChat(10);
    EXPECT_TRUE(session.personaBuildPending(10));

    std::string spec;
    std::string soul;
    ASSERT_TRUE(session.loadPersonaBuildMaterials(spec, soul));
    EXPECT_NE(spec.find("人格编译规范"), std::string::npos);
    EXPECT_NE(soul.find("克莱茵"), std::string::npos);

    // 编译失败（空产物）：标志保留，下轮重试
    session.applyGeneratedPersona(10, "");
    EXPECT_TRUE(session.personaBuildPending(10));
    EXPECT_NE(session.getUserConfig(10).system_prompt.find("克莱茵"), std::string::npos)
        << "失败时维持 soul.md 兜底";

    // skip：素材缺失语义，取消标志
    session.applyGeneratedPersona(10, "skip");
    EXPECT_FALSE(session.personaBuildPending(10));

    // 重新置位后成功应用：prompt 替换、标志清除
    session.resetChat(10);
    session.applyGeneratedPersona(10, "CHARACTER_KLEIN\nSTYLE_BRIEF");
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
    writeFile(dir / "persona-spec.md", kSampleSpec);

    ConversationStore store((dir / "conversation.db").string());
    ModelRegistry registry(writeModelRegistryFile(dir));
    ChatOptions options;
    options.defaultModel = "test-model";
    BotIdentity bot;
    UserSessionService session(registry, store, bot, options,
                               (dir / "soul.md").string(),
                               (dir / "persona-spec.md").string());

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

TEST(PersonaBuildLifecycleTest, MissingSpecDisablesBuild)
{
    const auto dir = std::filesystem::temp_directory_path() /
                     ("kleinbot-persona-c-" + std::to_string(::time(nullptr)));
    std::filesystem::create_directories(dir);
    writeModelRegistryFile(dir);
    writeFile(dir / "soul.md", kSampleSoul);
    // 不写 persona-spec.md

    ConversationStore store((dir / "conversation.db").string());
    ModelRegistry registry(writeModelRegistryFile(dir));
    ChatOptions options;
    options.defaultModel = "test-model";
    BotIdentity bot;
    UserSessionService session(registry, store, bot, options,
                               (dir / "soul.md").string(),
                               (dir / "persona-spec.md").string());

    session.ensureUserExists(10);
    session.resetChat(10);
    std::string spec;
    std::string soul;
    // 规范缺失：素材不可用 → 调用方走 skip 取消语义
    EXPECT_FALSE(session.loadPersonaBuildMaterials(spec, soul));

    std::filesystem::remove_all(dir);
}
