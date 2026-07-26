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
    std::size_t maxPendingMessages = 1024;
    std::size_t workerIdleSeconds = 30;
    long messageSurvivalSeconds = 3600;
    std::string privateAction = "send_private_msg";
    std::string groupAction = "send_group_msg";
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

struct ModelSchema
{
    std::string registryPath;
    ModelEndpointSchema drawing;
    ModelEndpointSchema vision;
    std::string stableDiffusionEndpoint;
    std::string stableDiffusionModel;
};

struct VoiceSchema
{
    bool enabled = false;
    std::string host;
    std::string port;
    std::string outputDirectory;
    std::string referenceAudioPath;
    std::string referenceText;
};

struct MemorySchema
{
    bool enabled = true;
    std::string model;
    std::size_t batchTurns = 3;
    std::size_t idleSeconds = 20;
    std::size_t recallLimit = 8;
};

struct StorageSchema
{
    std::string conversationDatabase = "source/conversations.db";
    std::string imageAssets = "source/image_assets";
};

struct ResourceSchema
{
    std::string personalityDirectory;
    std::string helpFile;
    std::string imageDownloadDirectory;
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

struct SchemaConfig
{
    int schemaVersion = 1;
    BotSchema bot;
    ChatSchema chat;
    ModelSchema models;
    VoiceSchema voice;
    bool accessibilityChat = false;
    MemorySchema memory;
    StorageSchema storage;
    ResourceSchema resources;
    std::string proxy;
    CommunicationSchema communication;
};

#endif
