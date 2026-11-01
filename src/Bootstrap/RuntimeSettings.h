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
#include "../Perception/GlanceOptions.h"
#include "../Push/PushSettings.h"
#include "../WebFetch/WebFetchOptions.h"
#include "../WebSearch/WebSearchOptions.h"

#include <string>

struct ModelRuntimeSettings
{
    ModelEndpointOptions drawing;
    ModelEndpointOptions vision;
    ModelEndpointOptions worker;
};

struct StorageRuntimeSettings
{
    std::string conversationDatabase;
    std::string imageAssets;
};

struct WebUiSettings
{
    bool enabled = false;
    std::string bind = "127.0.0.1";
    int port = kDefaultWebUiPort;
    std::string accessToken;
};

struct AffectSettings
{
    bool enabled = false;
    std::string policy = "baseline"; // baseline / judge
    bool enforce = false;            // false = judge 只记录计划（影子）
    std::string judgeModel;          // 空 = 用 models.worker
};

struct RuntimeSettings
{
    int schemaVersion = 1;
    BotIdentity bot;
    ChatOptions chat;
    ModelRuntimeSettings models;
    VoiceOptions voice;
    MemoryOptions memory;
    AffectSettings affect;
    GlanceOptions glance;
    WebSearchOptions webSearch;
    WebFetchOptions webFetch;
    MessageOptions message;
    MessageExecutionOptions messageExecution;
    DockOptions dock;
    StorageRuntimeSettings storage;
    TransportConfig transport;
    WebUiSettings webUi;
    PushSettings push;
};

RuntimeSettings buildRuntimeSettings(const SchemaConfig &schema);

#endif
