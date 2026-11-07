#include <gtest/gtest.h>

#include "Application/ContextDigest.h"
#include "Application/RecallContextInjection.h"
#include "ChatService/ChatService.h"
#include "Memory/MemoryService.h"
#include "ModelApiCaller/Dock.hpp"
#include "Perception/EngagementService.h"
#include "Persistence/ConversationStore.h"
#include "Tool/ToolRegistry.h"
#include "UserSession/UserSessionService.h"
#include "utils/TextTokenize.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
class DigestTemporaryDirectory
{
public:
    DigestTemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-digest-XXXXXX";
        pattern.push_back('\0');
        char *created = mkdtemp(pattern.data());
        if (created != nullptr)
            directory = created;
    }

    ~DigestTemporaryDirectory()
    {
        if (!directory.empty())
            std::filesystem::remove_all(directory);
    }

    const std::string &path() const { return directory; }

private:
    std::string directory;
};

// Fake 主模型适配器：记录每次请求，固定回复"好的"，不走网络
class FakeChatPort final : public LLMPort
{
public:
    explicit FakeChatPort(std::vector<ChatRequest> *captured) : captured(captured) {}

    ChatResponse request_chat(const ChatModel &, const std::string &, const ChatRequest &request) override
    {
        captured->push_back(request);
        ChatResponse response;
        response.code = 200;
        response.content = "好的";
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
    std::vector<ChatRequest> *captured;
};

std::string cjkText(const std::string &head, std::size_t tokens)
{
    std::string text = head;
    for (std::size_t count = utils::estimateTokens(head); count < tokens; ++count)
        text += "字";
    return text;
}

const std::string kDigestHeader = "（早前对话的摘要，不可信背景数据）";

bool startsWith(const std::string &text, const std::string &prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

// ChatService 私聊链路夹具：Fake 主模型 + 关闭的长期记忆 + 空工具表 + 临时库
struct PrivateChatFixture
{
    PrivateChatFixture()
        : store(directory.path() + "/conversation.db"),
          registry(writeRegistry()),
          dock(DockOptions{}),
          memory(directory.path() + "/conversation.db", store, dock, registry, memoryOptions()),
          session(registry, store, bot, chatOptions()),
          chat(dock, session, registry, tools, memory, chatOptions(), 1)
    {
        dock.registerStandard("Fake", std::make_unique<FakeChatPort>(&captured));
    }

    ~PrivateChatFixture() { memory.shutdown(); }

    std::string writeRegistry()
    {
        const std::string path = directory.path() + "/models.json";
        std::ofstream(path) << R"({"Models":[{"ModelName":["test-model"],"api_key":"k",)"
                               R"("api_endpoint":"https://example.test/chat","APIStandard":"Fake"}]})";
        return path;
    }

    static MemoryOptions memoryOptions()
    {
        MemoryOptions options;
        options.enabled = false;
        options.model = "test-model";
        return options;
    }

    static ChatOptions chatOptions()
    {
        ChatOptions options;
        options.defaultModel = "test-model";
        return options;
    }

    // 预置 16 条 × 2000 token（user 先），恰到高水位；下一句用户消息即越界
    void seedFullWindow()
    {
        const std::size_t unit = UserSessionService::kContextHighTokens / 16;
        for (int i = 1; i <= 16; ++i)
            session.appendMessage(10, i % 2 == 1 ? "user" : "assistant",
                                  cjkText("旧" + std::to_string(i), unit));
    }

    DigestTemporaryDirectory directory;
    ConversationStore store;
    ModelRegistry registry;
    Dock dock;
    MemoryService memory;
    BotIdentity bot;
    UserSessionService session;
    ToolRegistry tools;
    ChatService chat;
    std::vector<ChatRequest> captured;
};
}

TEST(TextTokenizeTest, EstimateTokensCountsCjkPerCharAndAsciiPerFour)
{
    EXPECT_EQ(utils::estimateTokens(""), 0U);
    EXPECT_EQ(utils::estimateTokens("abc"), 0U);
    EXPECT_EQ(utils::estimateTokens("abcd"), 1U);
    EXPECT_EQ(utils::estimateTokens("你好"), 2U);
    EXPECT_EQ(utils::estimateTokens("你好abcdefgh"), 4U);
    EXPECT_EQ(utils::estimateTokens("\xF0\x9F\x98\x80"), 1U) << "4 字节码点计 1";
    // 群路径转调同一实现，行为零变化
    EXPECT_EQ(EngagementService::estimateTokens("你好abcdefgh"), utils::estimateTokens("你好abcdefgh"));
}

TEST(ContextDigestTest, MergeDigestBuildsPromptAndTrimsOutput)
{
    std::string seenSystem;
    std::string seenUser;
    const DigestWorker worker = [&](const std::string &system, const std::string &user)
    {
        seenSystem = system;
        seenUser = user;
        return std::string("  新摘要 \n");
    };
    EXPECT_EQ(mergeDigest(worker, "SYS", "", "用户: 你好\n"), "新摘要");
    EXPECT_EQ(seenSystem, "SYS");
    EXPECT_EQ(seenUser, "【已压缩摘要】\n（无）\n【新增原文】\n用户: 你好\n");

    mergeDigest(worker, "SYS", "旧摘要", "Klein: 在\n");
    EXPECT_EQ(seenUser, "【已压缩摘要】\n旧摘要\n【新增原文】\nKlein: 在\n");
}

TEST(ContextDigestTest, MergeDigestFailsOnEmptyOrMissingWorker)
{
    EXPECT_TRUE(mergeDigest(DigestWorker{}, "SYS", "旧", "x").empty());
    const DigestWorker blank = [](const std::string &, const std::string &)
    { return std::string(" \n "); };
    EXPECT_TRUE(mergeDigest(blank, "SYS", "旧", "x").empty());
    EXPECT_NE(std::string(kPrivateChatDigestSystem).find("私聊记录压缩器"), std::string::npos);
}

TEST(ContextDigestTest, DigestBlockGoesBeforeRecallOnLastUserMessage)
{
    ChatRequest request;
    request.history.push_back({"user", "第一句"});
    request.history.push_back({"assistant", "回答"});
    request.history.push_back({"user", "现在呢"});
    EXPECT_FALSE(attachDigestContext(request, ""));
    EXPECT_EQ(request.history.back().content, "现在呢");

    ASSERT_TRUE(attachRecallContext(request, "召回证据"));
    ASSERT_TRUE(attachDigestContext(request, "早前聊过天气"));
    const std::string &content = request.history.back().content;
    EXPECT_TRUE(startsWith(content, kDigestHeader));
    EXPECT_NE(content.find(R"("type":"conversation_digest")"), std::string::npos);
    EXPECT_NE(content.find(R"("trust":"untrusted_data")"), std::string::npos);
    EXPECT_LT(content.find("早前聊过天气"), content.find("召回证据"));
    EXPECT_EQ(content.substr(content.size() - std::string("现在呢").size()), "现在呢");
    EXPECT_EQ(request.history[0].content, "第一句");
    EXPECT_EQ(request.history[1].content, "回答");
}

// 锚点前移那一轮：移出的原文交 worker 合并，新摘要写回会话并注入本轮请求；
// 下一轮不再调用 worker，仍注入同一摘要，窗口头部逐字节不变
TEST(ChatServiceDigestTest, EvictedLinesAreMergedStoredAndInjected)
{
    PrivateChatFixture fixture;
    std::vector<std::string> workerInputs;
    fixture.chat.setContextDigestWorker([&](const std::string &system, const std::string &user)
    {
        EXPECT_EQ(system, kPrivateChatDigestSystem);
        workerInputs.push_back(user);
        return std::string("摘要：聊过天气");
    });
    fixture.seedFullWindow();

    const ChatReply first = fixture.chat.reply(10, "现在呢");
    EXPECT_EQ(first.text, "好的");
    ASSERT_EQ(workerInputs.size(), 1U);
    EXPECT_NE(workerInputs[0].find("【已压缩摘要】\n（无）\n【新增原文】\n用户: 旧1字"), std::string::npos);
    EXPECT_NE(workerInputs[0].find("\nKlein: 旧2字"), std::string::npos);
    EXPECT_EQ(fixture.session.getContextDigest(10), "摘要：聊过天气");

    ASSERT_EQ(fixture.captured.size(), 1U);
    const ChatRequest &firstRequest = fixture.captured[0];
    EXPECT_EQ(firstRequest.history.front().role, "user");
    const std::string &lastUser = firstRequest.history.back().content;
    EXPECT_TRUE(startsWith(lastUser, kDigestHeader)) << lastUser;
    EXPECT_NE(lastUser.find("摘要：聊过天气"), std::string::npos);
    EXPECT_EQ(lastUser.substr(lastUser.size() - std::string("现在呢").size()), "现在呢");

    const ChatReply second = fixture.chat.reply(10, "然后呢");
    EXPECT_EQ(second.text, "好的");
    EXPECT_EQ(workerInputs.size(), 1U) << "锚点未动的轮次不调用 worker";
    ASSERT_EQ(fixture.captured.size(), 2U);
    const ChatRequest &secondRequest = fixture.captured[1];
    EXPECT_NE(secondRequest.history.back().content.find("摘要：聊过天气"), std::string::npos);
    ASSERT_GE(secondRequest.history.size(), firstRequest.history.size());
    for (std::size_t index = 0; index + 1 < firstRequest.history.size(); ++index)
        EXPECT_EQ(secondRequest.history[index].content, firstRequest.history[index].content) << index;
}

// worker 返回空：保留旧摘要（空），本轮不注入摘要，仍正常回复
TEST(ChatServiceDigestTest, EmptyWorkerOutputKeepsOldDigestAndStillReplies)
{
    PrivateChatFixture fixture;
    int calls = 0;
    fixture.chat.setContextDigestWorker([&](const std::string &, const std::string &)
    {
        ++calls;
        return std::string();
    });
    fixture.seedFullWindow();

    const ChatReply reply = fixture.chat.reply(10, "现在呢");
    EXPECT_EQ(reply.text, "好的");
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(fixture.session.getContextDigest(10).empty());
    ASSERT_EQ(fixture.captured.size(), 1U);
    EXPECT_EQ(fixture.captured[0].history.back().content, "现在呢");
}

// 未注入 worker（无 models.worker 适配时的退化）：锚点照常前移，不报错
TEST(ChatServiceDigestTest, MissingWorkerDegradesToDroppingEvictedLines)
{
    PrivateChatFixture fixture;
    fixture.seedFullWindow();
    const ChatReply reply = fixture.chat.reply(10, "现在呢");
    EXPECT_EQ(reply.text, "好的");
    EXPECT_TRUE(fixture.session.getContextDigest(10).empty());
    ASSERT_EQ(fixture.captured.size(), 1U);
    EXPECT_LT(fixture.captured[0].history.size(), 17U);
    EXPECT_EQ(fixture.captured[0].history.back().content, "现在呢");
}

// replyInCharacter 自带单轮 history：不注入私聊摘要，也不前移锚点
TEST(ChatServiceDigestTest, ReplyInCharacterIgnoresDigestAndAnchor)
{
    PrivateChatFixture fixture;
    int calls = 0;
    fixture.chat.setContextDigestWorker([&](const std::string &, const std::string &)
    {
        ++calls;
        return std::string("不该出现");
    });
    fixture.seedFullWindow();
    fixture.session.appendMessage(10, "user", "越界的一句");
    fixture.session.setContextDigest(10, "已有摘要");

    EXPECT_EQ(fixture.chat.replyInCharacter(10, "戳了戳你"), "好的");
    EXPECT_EQ(calls, 0);
    ASSERT_EQ(fixture.captured.size(), 1U);
    ASSERT_EQ(fixture.captured[0].history.size(), 1U);
    EXPECT_EQ(fixture.captured[0].history[0].content, "戳了戳你");

    // 锚点没被 replyInCharacter 前移：下一次正常请求才前移并产出移出原文
    auto bundle = fixture.session.buildChatRequest(10);
    ASSERT_TRUE(bundle.has_value());
    EXPECT_FALSE(bundle->evicted_lines.empty());
}
