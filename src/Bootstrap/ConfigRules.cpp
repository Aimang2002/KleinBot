#include "ConfigRules.h"
#include "LiveSettings.h"

namespace
{
constexpr ConfigChangeImpact Dynamic = ConfigChangeImpact::Dynamic;
constexpr ConfigChangeImpact Rebuild = ConfigChangeImpact::Rebuild;
constexpr ConfigChangeImpact Restart = ConfigChangeImpact::Restart;

// 应用函数：重载成功后，把新的运行时设置整份换进对应的活值（只替换快照，不会失败）。
// 一组一个；同组几个键一起变化时，ConfigReloader 只调一次

// chat：ChatOptions 里没有只能重启的字段（线程数在 MessageExecutionOptions），整份替换即可
void applyChat(LiveSettings &live, const RuntimeSettings &next)
{
    live.chat.set(next.chat);
}

// models：三个专用端点各是一个活值，各自整份替换（端点的四个字段都能实时生效）
void applyDrawingModel(LiveSettings &live, const RuntimeSettings &next)
{
    live.drawingModel.set(next.models.drawing);
}

void applyVisionModel(LiveSettings &live, const RuntimeSettings &next)
{
    live.visionModel.set(next.models.vision);
}

void applyWorkerModel(LiveSettings &live, const RuntimeSettings &next)
{
    live.workerModel.set(next.models.worker);
}

// 每行：差异里的路径、生效方式、比较的 Schema 字段。表的顺序就是差异条目的输出顺序。
// applies(应用函数) 的键重载成功后立即生效；restart() 的键只能重启生效；
// notYet(档位) 的键还没接通实时生效，档位保持改造前的值
constexpr ConfigRule kProductRules[] = {
    {"schema_version", restart(), field<&SchemaConfig::schemaVersion>},

    // bot
    {"bot.id",                 restart(),       field<&SchemaConfig::bot, &BotSchema::id>},
    {"bot.manager_id",         restart(),       field<&SchemaConfig::bot, &BotSchema::managerId>},
    {"bot.name",               notYet(Restart), field<&SchemaConfig::bot, &BotSchema::name>},
    {"bot.aliases",            notYet(Restart), field<&SchemaConfig::bot, &BotSchema::aliases>},
    {"bot.group_chat_enabled", notYet(Dynamic), field<&SchemaConfig::bot, &BotSchema::groupChatEnabled>},

    // chat：top_p 还没送进请求、修法待定，尚未接通；worker_threads 只能重启
    {"chat.default_model",            applies(applyChat), field<&SchemaConfig::chat, &ChatSchema::defaultModel>},
    {"chat.temperature",              applies(applyChat), field<&SchemaConfig::chat, &ChatSchema::temperature>},
    {"chat.top_p",                    notYet(Rebuild),    field<&SchemaConfig::chat, &ChatSchema::topP>},
    {"chat.frequency_penalty",        applies(applyChat), field<&SchemaConfig::chat, &ChatSchema::frequencyPenalty>},
    {"chat.presence_penalty",         applies(applyChat), field<&SchemaConfig::chat, &ChatSchema::presencePenalty>},
    {"chat.max_message_tokens",       applies(applyChat), field<&SchemaConfig::chat, &ChatSchema::maxMessageTokens>},
    {"chat.worker_threads",           restart(),          field<&SchemaConfig::chat, &ChatSchema::workerThreads>},
    {"chat.message_survival_seconds", applies(applyChat), field<&SchemaConfig::chat, &ChatSchema::messageSurvivalSeconds>},

    // models：drawing / vision / worker 各四个字段。三个端点来源每次调用时读活值
    // （judge 例外：它的端点与记录标签到 L2.c 才改成按当前配置解析，见 main.cpp）
    {"models.drawing.model", applies(applyDrawingModel),
     field<&SchemaConfig::models, &ModelSchema::drawing, &ModelEndpointSchema::model>},
    {"models.drawing.endpoint", applies(applyDrawingModel),
     field<&SchemaConfig::models, &ModelSchema::drawing, &ModelEndpointSchema::endpoint>},
    {"models.drawing.api_key", applies(applyDrawingModel),
     field<&SchemaConfig::models, &ModelSchema::drawing, &ModelEndpointSchema::apiKey>},
    {"models.drawing.api_standard", applies(applyDrawingModel),
     field<&SchemaConfig::models, &ModelSchema::drawing, &ModelEndpointSchema::apiStandard>},
    {"models.vision.model", applies(applyVisionModel),
     field<&SchemaConfig::models, &ModelSchema::vision, &ModelEndpointSchema::model>},
    {"models.vision.endpoint", applies(applyVisionModel),
     field<&SchemaConfig::models, &ModelSchema::vision, &ModelEndpointSchema::endpoint>},
    {"models.vision.api_key", applies(applyVisionModel),
     field<&SchemaConfig::models, &ModelSchema::vision, &ModelEndpointSchema::apiKey>},
    {"models.vision.api_standard", applies(applyVisionModel),
     field<&SchemaConfig::models, &ModelSchema::vision, &ModelEndpointSchema::apiStandard>},
    {"models.worker.model", applies(applyWorkerModel),
     field<&SchemaConfig::models, &ModelSchema::worker, &ModelEndpointSchema::model>},
    {"models.worker.endpoint", applies(applyWorkerModel),
     field<&SchemaConfig::models, &ModelSchema::worker, &ModelEndpointSchema::endpoint>},
    {"models.worker.api_key", applies(applyWorkerModel),
     field<&SchemaConfig::models, &ModelSchema::worker, &ModelEndpointSchema::apiKey>},
    {"models.worker.api_standard", applies(applyWorkerModel),
     field<&SchemaConfig::models, &ModelSchema::worker, &ModelEndpointSchema::apiStandard>},

    // voice
    {"voice.enabled",              notYet(Dynamic), field<&SchemaConfig::voice, &VoiceSchema::enabled>},
    {"voice.host",                 notYet(Rebuild), field<&SchemaConfig::voice, &VoiceSchema::host>},
    {"voice.port",                 notYet(Rebuild), field<&SchemaConfig::voice, &VoiceSchema::port>},
    {"voice.reference_audio_path", notYet(Rebuild), field<&SchemaConfig::voice, &VoiceSchema::referenceAudioPath>},
    {"voice.reference_text",       notYet(Rebuild), field<&SchemaConfig::voice, &VoiceSchema::referenceText>},

    // memory
    {"memory.enabled",      notYet(Rebuild), field<&SchemaConfig::memory, &MemorySchema::enabled>},
    {"memory.model",        notYet(Rebuild), field<&SchemaConfig::memory, &MemorySchema::model>},
    {"memory.batch_turns",  notYet(Rebuild), field<&SchemaConfig::memory, &MemorySchema::batchTurns>},
    {"memory.idle_minutes", notYet(Rebuild), field<&SchemaConfig::memory, &MemorySchema::idleMinutes>},
    {"memory.recall_limit", notYet(Rebuild), field<&SchemaConfig::memory, &MemorySchema::recallLimit>},

    // affect
    {"affect.enabled",     notYet(Restart), field<&SchemaConfig::affect, &AffectSchema::enabled>},
    {"affect.policy",      notYet(Restart), field<&SchemaConfig::affect, &AffectSchema::policy>},
    {"affect.enforce",     notYet(Restart), field<&SchemaConfig::affect, &AffectSchema::enforce>},
    {"affect.judge_model", notYet(Restart), field<&SchemaConfig::affect, &AffectSchema::judgeModel>},

    // glance
    {"glance.enabled",              notYet(Restart), field<&SchemaConfig::glance, &GlanceSchema::enabled>},
    {"glance.per_group_per_minute", notYet(Restart), field<&SchemaConfig::glance, &GlanceSchema::perGroupPerMinute>},

    // web_search：provider 决定构造哪一种搜索实现，只能重启生效；档位保持改造前的 Rebuild
    {"web_search.enabled",            notYet(Rebuild), field<&SchemaConfig::webSearch, &WebSearchSchema::enabled>},
    {"web_search.provider",           notYet(Rebuild), field<&SchemaConfig::webSearch, &WebSearchSchema::provider>},
    {"web_search.endpoint",           notYet(Rebuild), field<&SchemaConfig::webSearch, &WebSearchSchema::endpoint>},
    {"web_search.api_key",            notYet(Rebuild), field<&SchemaConfig::webSearch, &WebSearchSchema::apiKey>},
    {"web_search.search_depth",       notYet(Rebuild), field<&SchemaConfig::webSearch, &WebSearchSchema::searchDepth>},
    {"web_search.max_results",        notYet(Rebuild), field<&SchemaConfig::webSearch, &WebSearchSchema::maxResults>},
    {"web_search.max_content_chars",  notYet(Rebuild), field<&SchemaConfig::webSearch, &WebSearchSchema::maxContentChars>},
    {"web_search.max_response_bytes", notYet(Rebuild), field<&SchemaConfig::webSearch, &WebSearchSchema::maxResponseBytes>},
    {"web_search.connect_timeout_ms", notYet(Rebuild), field<&SchemaConfig::webSearch, &WebSearchSchema::connectTimeoutMs>},
    {"web_search.request_timeout_ms", notYet(Rebuild), field<&SchemaConfig::webSearch, &WebSearchSchema::requestTimeoutMs>},

    // web_fetch
    {"web_fetch.enabled",            notYet(Rebuild), field<&SchemaConfig::webFetch, &WebFetchSchema::enabled>},
    {"web_fetch.max_content_chars",  notYet(Rebuild), field<&SchemaConfig::webFetch, &WebFetchSchema::maxContentChars>},
    {"web_fetch.max_response_bytes", notYet(Rebuild), field<&SchemaConfig::webFetch, &WebFetchSchema::maxResponseBytes>},
    {"web_fetch.connect_timeout_ms", notYet(Rebuild), field<&SchemaConfig::webFetch, &WebFetchSchema::connectTimeoutMs>},
    {"web_fetch.request_timeout_ms", notYet(Rebuild), field<&SchemaConfig::webFetch, &WebFetchSchema::requestTimeoutMs>},
    {"web_fetch.cache_ttl_seconds",  notYet(Rebuild), field<&SchemaConfig::webFetch, &WebFetchSchema::cacheTtlSeconds>},
    {"web_fetch.cache_max_entries",  notYet(Rebuild), field<&SchemaConfig::webFetch, &WebFetchSchema::cacheMaxEntries>},

    // storage
    {"storage.conversation_database", restart(), field<&SchemaConfig::storage, &StorageSchema::conversationDatabase>},
    {"storage.image_assets",          restart(), field<&SchemaConfig::storage, &StorageSchema::imageAssets>},

    // network：代理同时作用于模型调用、搜索、抓取
    {"network.proxy", notYet(Rebuild), field<&SchemaConfig::proxy>},

    // communication：通信方式与协议端地址只能重启生效；档位保持改造前的 Rebuild。
    // active_profile 指 active_transport 选中的那一份传输档案
    {"communication.protocol.type", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::protocolType>},
    {"communication.active_transport", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeTransport>},
    {"communication.active_profile.type", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::type>},
    {"communication.active_profile.host", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::host>},
    {"communication.active_profile.port", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::port>},
    {"communication.active_profile.path", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::path>},
    {"communication.active_profile.access_token", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::accessToken>},
    {"communication.active_profile.api.base_url", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::apiBaseUrl>},
    {"communication.active_profile.api.access_token", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::apiAccessToken>},
    {"communication.active_profile.events.bind", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::eventBindHost>},
    {"communication.active_profile.events.port", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::eventBindPort>},
    {"communication.active_profile.events.path", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::eventPath>},
    {"communication.active_profile.events.access_token", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::eventAccessToken>},
    {"communication.active_profile.events.secret", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::activeProfile, &TransportProfileSchema::eventSecret>},
    {"communication.defaults.connect_timeout_ms", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::connectTimeoutMs>},
    {"communication.defaults.request_timeout_ms", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::requestTimeoutMs>},
    {"communication.defaults.max_event_body_bytes", notYet(Rebuild),
     field<&SchemaConfig::communication, &CommunicationSchema::maxBodyBytes>},

    // webui
    {"webui.enabled",      restart(), field<&SchemaConfig::webUi, &WebUiSchema::enabled>},
    {"webui.bind",         restart(), field<&SchemaConfig::webUi, &WebUiSchema::bind>},
    {"webui.port",         restart(), field<&SchemaConfig::webUi, &WebUiSchema::port>},
    {"webui.access_token", restart(), field<&SchemaConfig::webUi, &WebUiSchema::accessToken>},

    // push（推送门运行时搁置中）
    {"push.enabled",        restart(), field<&SchemaConfig::push, &PushSchema::enabled>},
    {"push.bind",           restart(), field<&SchemaConfig::push, &PushSchema::bind>},
    {"push.port",           restart(), field<&SchemaConfig::push, &PushSchema::port>},
    {"push.access_token",   restart(), field<&SchemaConfig::push, &PushSchema::accessToken>},
    {"push.allowed_groups", restart(), field<&SchemaConfig::push, &PushSchema::allowedGroups>},
    {"push.allowed_users",  restart(), field<&SchemaConfig::push, &PushSchema::allowedUsers>},
    {"push.max_per_hour",   restart(), field<&SchemaConfig::push, &PushSchema::maxPerHour>},
};
}

ConfigRuleTable productConfigRules()
{
    return ruleTable(kProductRules);
}
