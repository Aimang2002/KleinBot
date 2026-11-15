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
