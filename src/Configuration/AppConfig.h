#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "../Network/TransportConfig.h"

#include <cstddef>
#include <cstdint>
#include <string>

struct BotConfig
{
    std::uint64_t id = 0;
    std::uint64_t managerId = 0;
    std::string name = "Klein";
    bool groupChatEnabled = true;
};

struct ChatConfig
{
    std::string defaultModel;
    double temperature = 1.0;
    double topP = 1.0;
    double frequencyPenalty = 0.0;
    double presencePenalty = 0.0;
    std::size_t maxMessageTokens = 4096;
    std::size_t workerThreads = 4;
    long messageSurvivalSeconds = 3600;
    std::string privateAction = "send_private_msg";
    std::string groupAction = "send_group_msg";
};

struct ModelEndpointConfig
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

struct ModelConfig
{
    std::string registryPath;
    ModelEndpointConfig drawing;
    ModelEndpointConfig vision;
    std::string stableDiffusionEndpoint;
    std::string stableDiffusionModel;
};

struct VoiceConfig
{
    bool enabled = false;
    std::string host;
    std::string port;
    std::string outputDirectory;
    std::string referenceAudioPath;
    std::string referenceText;
};

struct FeatureConfig
{
    bool accessibilityChat = false;
};

struct MemoryConfig
{
    bool enabled = true;
    std::string model;
    std::size_t batchTurns = 3;
    std::size_t idleSeconds = 20;
    std::size_t recallLimit = 8;
};

struct StorageConfig
{
    std::string conversationDatabase = "source/conversations.db";
    std::string imageAssets = "source/image_assets";
};

struct ResourceConfig
{
    std::string personalityDirectory;
    std::string helpFile;
    std::string imageDownloadDirectory;
};

struct NetworkConfig
{
    std::string proxy;
};

struct AppConfig
{
    int schemaVersion = 1;
    BotConfig bot;
    ChatConfig chat;
    ModelConfig models;
    VoiceConfig voice;
    FeatureConfig features;
    MemoryConfig memory;
    StorageConfig storage;
    ResourceConfig resources;
    NetworkConfig network;
    TransportConfig transport;
};

#endif
