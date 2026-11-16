#include <gtest/gtest.h>

#include "ModelApiCaller/Voice/Voice.h"
#include "utils/Live.h"
#include "../Library/httplib/httplib.h"
#include "../Library/nlohmann/json.hpp"

#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
// 本地假 TTS 服务：记下收到的请求体，回几个字节当音频
class FakeTtsServer
{
public:
    FakeTtsServer()
    {
        server.Post("/tts", [this](const httplib::Request &request, httplib::Response &response) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                bodies.push_back(nlohmann::json::parse(request.body));
            }
            response.set_content("RIFF-fake-wav", "audio/wav");
        });
        port = server.bind_to_any_port("127.0.0.1");
        thread = std::thread([this]() { server.listen_after_bind(); });
    }

    ~FakeTtsServer()
    {
        server.stop();
        thread.join();
    }

    std::vector<nlohmann::json> received() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return bodies;
    }

    int port = 0;

private:
    httplib::Server server;
    std::thread thread;
    mutable std::mutex mutex;
    std::vector<nlohmann::json> bodies;
};

VoiceOptions optionsFor(int port, const std::string &reference)
{
    VoiceOptions options;
    options.enabled = true;
    options.host = "127.0.0.1";
    options.port = std::to_string(port);
    options.referenceAudioPath = reference + ".wav";
    options.referenceText = reference;
    return options;
}

void removeAudio(const std::string &path)
{
    std::error_code error;
    std::filesystem::remove(path, error);
}
}

// 配置实时生效（L1.c）：Voice 每次合成开头取一份活值快照。先合成一次，再换地址与参考音频：
// 下一次合成就发往新地址、带新参数，之前那次不受影响
TEST(VoiceLiveConfigTest, NextSynthesisUsesReplacedOptions)
{
    FakeTtsServer first;
    FakeTtsServer second;
    ASSERT_GT(first.port, 0);
    ASSERT_GT(second.port, 0);
    Live<VoiceOptions> options(optionsFor(first.port, "参考A"));
    Voice voice(options);

    const std::string before = voice.toAudio("你好");
    ASSERT_TRUE(std::filesystem::exists(before)) << before;
    removeAudio(before);
    ASSERT_EQ(first.received().size(), 1U);
    EXPECT_EQ(first.received()[0]["prompt_text"], "参考A");
    EXPECT_EQ(first.received()[0]["ref_audio_path"], "参考A.wav");

    options.set(optionsFor(second.port, "参考B"));
    const std::string after = voice.toAudio("再见");
    ASSERT_TRUE(std::filesystem::exists(after)) << after;
    removeAudio(after);
    EXPECT_EQ(first.received().size(), 1U) << "换值之后不再发往旧地址";
    ASSERT_EQ(second.received().size(), 1U);
    EXPECT_EQ(second.received()[0]["text"], "再见");
    EXPECT_EQ(second.received()[0]["prompt_text"], "参考B");
    EXPECT_EQ(second.received()[0]["ref_audio_path"], "参考B.wav");
}

// ===== 语音由她决定（2026-11-16）：send_voice 工具与语音服务可用性 =====
#include "Tool/SendVoiceTool.h"

namespace
{
// 回 500 的假 TTS 服务：响应体不是音频
class FailingTtsServer
{
public:
    FailingTtsServer()
    {
        server.Post("/tts", [](const httplib::Request &, httplib::Response &response) {
            response.status = 500;
            response.set_content(R"({"message":"bad ref audio"})", "application/json");
        });
        port = server.bind_to_any_port("127.0.0.1");
        thread = std::thread([this]() { server.listen_after_bind(); });
    }
    ~FailingTtsServer()
    {
        server.stop();
        thread.join();
    }
    int port = -1;

private:
    httplib::Server server;
    std::thread thread;
};

ToolContext anyContext()
{
    return ToolContext{7, 0, std::string(), 0};
}
} // namespace

// 服务回了错误：响应体不是音频，不能当语音发出去；失败一律返回空串、服务记为不可用
TEST(VoiceServiceTest, ErrorStatusIsAFailureNotAnAudioFile)
{
    FailingTtsServer failing;
    ASSERT_GT(failing.port, 0);
    Live<VoiceOptions> options(optionsFor(failing.port, "参考"));
    Voice voice(options);
    EXPECT_TRUE(voice.toAudio("你好").empty());
    EXPECT_FALSE(voice.reachable()) << "合成失败后直到下一次探测都按不可用";
}

TEST(VoiceServiceTest, ReachableProbesTheConfiguredAddress)
{
    FakeTtsServer server;
    ASSERT_GT(server.port, 0);
    Live<VoiceOptions> options(optionsFor(server.port, "参考"));
    Voice voice(options);
    EXPECT_TRUE(voice.reachable());
    EXPECT_TRUE(server.received().empty()) << "探测只连一下，不合成";

    // 换成没人监听的地址：地址变了立即重新探测
    options.set(optionsFor(1, "参考"));
    EXPECT_FALSE(voice.reachable());
    EXPECT_TRUE(voice.toAudio("你好").empty());
}

// 开放条件：总开关开着且服务可用
TEST(SendVoiceToolTest, OfferedOnlyWhenSwitchedOnAndServiceReachable)
{
    Live<bool> globalVoice{false};
    bool reachable = true;
    SendVoiceTool tool(globalVoice, [](const std::string &) { return std::string("a.wav"); },
                       [&reachable] { return reachable; });
    EXPECT_FALSE(tool.available()) << "总开关关着";
    globalVoice.set(true);
    EXPECT_TRUE(tool.available());
    reachable = false;
    EXPECT_FALSE(tool.available()) << "服务不可用";
    EXPECT_TRUE(tool.offeredIn(ToolVenue::Private));
    EXPECT_TRUE(tool.offeredIn(ToolVenue::Group));
    // schema 写清只在明确要求时用
    EXPECT_NE(tool.description().find("明确要求"), std::string::npos);
    EXPECT_NE(SendVoiceTool::guidanceText(GuidanceScene::Conversation).find("其余情况都用文字回复"),
              std::string::npos);
    EXPECT_NE(SendVoiceTool::guidanceText(GuidanceScene::GroupTurn).find("group_say"),
              std::string::npos);
}

// 合成成功：语音出站带原话，终止本轮、不再另发文字，写进历史的是原话
TEST(SendVoiceToolTest, SpeaksTheTextAsVoiceAndKeepsTheWords)
{
    Live<bool> globalVoice{true};
    std::string synthesized;
    SendVoiceTool tool(globalVoice, [&synthesized](const std::string &text)
                       {
                           synthesized = text;
                           return std::string("/tmp/kleinbot/a.wav");
                       },
                       [] { return true; });
    const ToolResult result = tool.execute(R"({"text":"  晚上好，博士  "})", anyContext());
    EXPECT_EQ(synthesized, "晚上好，博士");
    EXPECT_TRUE(result.terminal);
    EXPECT_TRUE(result.suppress_text_reply);
    EXPECT_EQ(result.model_content, "晚上好，博士");
    ASSERT_EQ(result.outbound_messages.size(), 1U);
    const auto *voice = std::get_if<VoiceMessage>(&result.outbound_messages.front());
    ASSERT_NE(voice, nullptr);
    EXPECT_EQ(voice->audio_path, "/tmp/kleinbot/a.wav");
    EXPECT_EQ(voice->transcript, "晚上好，博士");
}

// 合成失败：用文字发出原话，失败不顶替内容（2026-11-16 之前发的是“系统提示：语音模块异常。”）
TEST(SendVoiceToolTest, FallsBackToTheSameWordsAsText)
{
    Live<bool> globalVoice{true};
    SendVoiceTool tool(globalVoice, [](const std::string &) { return std::string(); },
                       [] { return true; });
    const ToolResult result = tool.execute(R"({"text":"好，我退开了"})", anyContext());
    EXPECT_TRUE(result.terminal);
    EXPECT_TRUE(result.suppress_text_reply);
    ASSERT_EQ(result.outbound_messages.size(), 1U);
    const auto *text = std::get_if<TextMessage>(&result.outbound_messages.front());
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->content, "好，我退开了");

    const ToolResult empty = tool.execute(R"({"text":"   "})", anyContext());
    EXPECT_FALSE(empty.terminal) << "没有可说的话：回灌错误让她改用文字";
    EXPECT_TRUE(empty.outbound_messages.empty());
}
