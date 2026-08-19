#ifndef RUNTIME_SETTINGS_H
#define RUNTIME_SETTINGS_H

#include "../Application/BotIdentity.h"
#include "../Application/MessageExecutionOptions.h"
#include "../ChatService/ChatOptions.h"
#include "../Configuration/SchemaConfig.h"
#include "../Memory/MemoryOptions.h"
#include "../Message/MessageOptions.h"
#include "../ModelApiCaller/DockOptions.h"
#include "../ModelApiCaller/ModelEndpointOptions.h"
#include "../ModelApiCaller/Voice/VoiceOptions.h"
#include "../Network/TransportConfig.h"
#include "../WebFetch/WebFetchOptions.h"
#include "../WebSearch/WebSearchOptions.h"

#include <string>

struct ModelRuntimeSettings
{
    std::string registryPath;
    ModelEndpointOptions drawing;
    ModelEndpointOptions vision;
    std::string stableDiffusionEndpoint;
    std::string stableDiffusionModel;
};

struct StorageRuntimeSettings
{
    std::string conversationDatabase;
    std::string imageAssets;
};

struct ResourceRuntimeSettings
{
    std::string personalityDirectory;
    std::string helpFile;
    std::string imageDownloadDirectory;
};

struct RuntimeSettings
{
    int schemaVersion = 1;
    BotIdentity bot;
    ChatOptions chat;
    ModelRuntimeSettings models;
    VoiceOptions voice;
    MemoryOptions memory;
    WebSearchOptions webSearch;
    WebFetchOptions webFetch;
    MessageOptions message;
    MessageExecutionOptions messageExecution;
    DockOptions dock;
    StorageRuntimeSettings storage;
    ResourceRuntimeSettings resources;
    TransportConfig transport;
};

RuntimeSettings buildRuntimeSettings(const SchemaConfig &schema);

#endif
