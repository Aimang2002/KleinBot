#ifndef SCHEMA_CONFIG_H
#define SCHEMA_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

struct BotSchema
{
    std::uint64_t id = 0;
    std::uint64_t managerId = 0;
    std::string name = "Klein";
    bool groupChatEnabled = true;
};

struct ChatSchema
{
    std::string defaultModel;
    double temperature = 1.0;
    double topP = 1.0;
    double frequencyPenalty = 0.0;
    double presencePenalty = 0.0;
    std::size_t maxMessageTokens = 4096;
    std::optional<std::size_t> workerThreads;
    long messageSurvivalSeconds = 3600;
};

struct ModelEndpointSchema
{
    std::string model;
    std::string endpoint;
    std::string apiKey;
    std::string apiStandard;

    bool configured() const
    {
        return !model.empty() && !endpoint.empty() && !apiStandard.empty();
    }
};

// 模型注册表路径钉死为编译期常量（相对工作目录）：可配置没有真实用例，
// 反而多一种“路径写错 → 注册表悄悄为空”的故障模式；旧 config 里的 registry_path 静默忽略
inline constexpr const char *kModelRegistryPath = "source/Model/ModelsName.json";

struct ModelSchema
{
    ModelEndpointSchema drawing;
    ModelEndpointSchema vision;
};

struct VoiceSchema
{
    bool enabled = false;
    std::string host;
    std::string port;
    std::string referenceAudioPath;
    std::string referenceText;
};

struct MemorySchema
{
    bool enabled = true;
    std::string model;
    std::size_t batchTurns = 3;
    std::size_t idleMinutes = 1;
    std::size_t recallLimit = 8;
};

struct WebSearchSchema
{
    bool enabled = false;
    std::string provider = "tavily";
    std::string endpoint = "https://api.tavily.com/search";
    std::string apiKey;
    std::string searchDepth = "basic";
    std::size_t maxResults = 5;
    std::size_t maxContentChars = 2000;
    std::size_t maxResponseBytes = 2097152;
    long connectTimeoutMs = 5000;
    long requestTimeoutMs = 15000;
};

struct WebFetchSchema
{
    bool enabled = false;
    std::size_t maxContentChars = 12000;
    std::size_t maxResponseBytes = 2097152;
    long connectTimeoutMs = 5000;
    long requestTimeoutMs = 20000;
    long cacheTtlSeconds = 900;
    std::size_t cacheMaxEntries = 32;
};

struct StorageSchema
{
    // 数据库与配置文件默认以点前缀隐藏（ls 默认不显示）；老路径无自动迁移，升级需手动改名
    std::string conversationDatabase = "source/.conversations.db";
    std::string imageAssets = "source/image_assets";
};

struct TransportProfileSchema
{
    std::string type;
    std::string host;
    unsigned short port = 0;
    std::string path = "/";
    std::string accessToken;
    std::string apiBaseUrl;
    std::string apiAccessToken;
    std::string eventBindHost = "127.0.0.1";
    unsigned short eventBindPort = 0;
    std::string eventPath = "/onebot/events";
    std::string eventAccessToken;
    std::string eventSecret;
};

struct CommunicationSchema
{
    std::string protocolType;
    std::string activeTransport;
    TransportProfileSchema activeProfile;
    long connectTimeoutMs = 5000;
    long requestTimeoutMs = 15000;
    std::size_t maxBodyBytes = 1048576;
};

// Web 面板默认端口：KLEIN 在手机九宫格键盘上的映射（K=5, L=5, E=3, I=4, N=6）
inline constexpr int kDefaultWebUiPort = 55346;

struct WebUiSchema
{
    bool enabled = false;
    std::string bind = "127.0.0.1";
    int port = kDefaultWebUiPort;
    std::string accessToken;
};

struct SchemaConfig
{
    int schemaVersion = 1;
    BotSchema bot;
    ChatSchema chat;
    ModelSchema models;
    VoiceSchema voice;
    MemorySchema memory;
    WebSearchSchema webSearch;
    WebFetchSchema webFetch;
    StorageSchema storage;
    std::string proxy;
    CommunicationSchema communication;
    WebUiSchema webUi;
};

#endif
