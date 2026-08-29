#include "ConfigLoader.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace
{
using json = nlohmann::json;

class Decoder
{
public:
    explicit Decoder(std::vector<ConfigDiagnostic> &diagnostics) : diagnostics(diagnostics) {}

    void diagnostic(ConfigSeverity severity, ConfigErrorCategory category,
                    const std::string &path, const std::string &message)
    {
        diagnostics.push_back({severity, category, path, message});
    }

    void unknownFields(const json &object, const std::set<std::string> &allowed,
                       const std::string &path)
    {
        if (!object.is_object())
            return;
        for (const auto &entry : object.items())
        {
            if (allowed.find(entry.key()) == allowed.end())
            {
                diagnostic(ConfigSeverity::Warning, ConfigErrorCategory::Unknown,
                           path + "." + entry.key(), "未知字段已忽略");
            }
        }
    }

    const json *object(const json &parent, const std::string &key, const std::string &path,
                       bool required = false)
    {
        auto iterator = parent.find(key);
        if (iterator == parent.end())
        {
            if (required)
                diagnostic(ConfigSeverity::Fatal, ConfigErrorCategory::Missing,
                           path, "缺少必需对象");
            return nullptr;
        }
        if (!iterator->is_object())
        {
            diagnostic(required ? ConfigSeverity::Fatal : ConfigSeverity::Warning,
                       ConfigErrorCategory::Type, path, "必须是对象");
            return nullptr;
        }
        return &*iterator;
    }

    std::string string(const json &parent, const std::string &key, const std::string &path,
                       const std::string &defaultValue = {}, bool required = false)
    {
        auto iterator = parent.find(key);
        if (iterator == parent.end())
        {
            if (required)
                diagnostic(ConfigSeverity::Fatal, ConfigErrorCategory::Missing,
                           path, "缺少必需字符串");
            else if (!defaultValue.empty())
                diagnostic(ConfigSeverity::Info, ConfigErrorCategory::Missing,
                           path, "字段缺失，使用默认值");
            return defaultValue;
        }
        if (!iterator->is_string())
        {
            diagnostic(required ? ConfigSeverity::Fatal : ConfigSeverity::Warning,
                       ConfigErrorCategory::Type, path, "必须是字符串");
            return defaultValue;
        }
        std::string value = iterator->get<std::string>();
        if (required && value.empty())
            diagnostic(ConfigSeverity::Fatal, ConfigErrorCategory::Missing, path, "不能为空");
        return value;
    }

    std::string secret(const json &parent, const std::string &key, const std::string &path,
                       ConfigSeverity missingSeverity = ConfigSeverity::Error)
    {
        auto iterator = parent.find(key);
        if (iterator == parent.end() || iterator->is_null())
            return {};
        if (iterator->is_string())
            return iterator->get<std::string>();
        if (!iterator->is_object())
        {
            diagnostic(ConfigSeverity::Warning, ConfigErrorCategory::Type,
                       path, "Secret 必须是字符串或 {literal/from_env} 对象");
            return {};
        }
        unknownFields(*iterator, {"literal", "from_env"}, path);
        if (iterator->contains("from_env"))
        {
            if (iterator->contains("literal"))
                diagnostic(ConfigSeverity::Warning, ConfigErrorCategory::Security,
                           path, "同时配置 literal 和 from_env，优先使用 from_env");
            const std::string variable = string(*iterator, "from_env", path + ".from_env", {}, true);
            const char *value = variable.empty() ? nullptr : std::getenv(variable.c_str());
            if (value == nullptr)
            {
                diagnostic(missingSeverity, ConfigErrorCategory::Security,
                           path, "指定的环境变量不存在");
                return {};
            }
            return value;
        }
        return string(*iterator, "literal", path + ".literal");
    }

    bool boolean(const json &parent, const std::string &key, const std::string &path,
                 bool defaultValue)
    {
        auto iterator = parent.find(key);
        if (iterator == parent.end())
            return defaultValue;
        if (!iterator->is_boolean())
        {
            diagnostic(ConfigSeverity::Warning, ConfigErrorCategory::Type,
                       path, "必须是布尔值，已使用默认值");
            return defaultValue;
        }
        return iterator->get<bool>();
    }

    long integer(const json &parent, const std::string &key, const std::string &path,
                 long defaultValue, long minimum, long maximum, bool required = false)
    {
        auto iterator = parent.find(key);
        if (iterator == parent.end())
        {
            if (required)
                diagnostic(ConfigSeverity::Fatal, ConfigErrorCategory::Missing, path, "缺少必需整数");
            return defaultValue;
        }
        if (!iterator->is_number_integer())
        {
            diagnostic(required ? ConfigSeverity::Fatal : ConfigSeverity::Warning,
                       ConfigErrorCategory::Type, path, "必须是整数，已使用默认值");
            return defaultValue;
        }
        const long long value = iterator->get<long long>();
        if (value < minimum || value > maximum)
        {
            diagnostic(required ? ConfigSeverity::Fatal : ConfigSeverity::Warning,
                       ConfigErrorCategory::Range, path, "数值超出允许范围，已使用默认值");
            return defaultValue;
        }
        return static_cast<long>(value);
    }

    std::uint64_t unsignedInteger(const json &parent, const std::string &key,
                                  const std::string &path, bool required)
    {
        auto iterator = parent.find(key);
        if (iterator == parent.end())
        {
            if (required)
                diagnostic(ConfigSeverity::Fatal, ConfigErrorCategory::Missing, path, "缺少必需正整数");
            return 0;
        }
        if (!iterator->is_number_unsigned() && !iterator->is_number_integer())
        {
            diagnostic(required ? ConfigSeverity::Fatal : ConfigSeverity::Warning,
                       ConfigErrorCategory::Type, path, "必须是正整数");
            return 0;
        }
        const long long value = iterator->get<long long>();
        if (value <= 0)
        {
            diagnostic(required ? ConfigSeverity::Fatal : ConfigSeverity::Warning,
                       ConfigErrorCategory::Range, path, "必须大于零");
            return 0;
        }
        return static_cast<std::uint64_t>(value);
    }

    double number(const json &parent, const std::string &key, const std::string &path,
                  double defaultValue, double minimum, double maximum)
    {
        auto iterator = parent.find(key);
        if (iterator == parent.end())
            return defaultValue;
        if (!iterator->is_number())
        {
            diagnostic(ConfigSeverity::Warning, ConfigErrorCategory::Type,
                       path, "必须是数值，已使用默认值");
            return defaultValue;
        }
        const double value = iterator->get<double>();
        if (value < minimum || value > maximum)
        {
            diagnostic(ConfigSeverity::Warning, ConfigErrorCategory::Range,
                       path, "数值超出允许范围，已使用默认值");
            return defaultValue;
        }
        return value;
    }

private:
    std::vector<ConfigDiagnostic> &diagnostics;
};

std::string normalizedPath(Decoder &decoder, const std::string &path, const std::string &value)
{
    if (value.empty())
        return "/";
    if (value.front() != '/')
    {
        decoder.diagnostic(ConfigSeverity::Error, ConfigErrorCategory::Range,
                           path, "路径必须以 / 开头");
        return "/";
    }
    return value;
}

ModelEndpointSchema decodeModelEndpoint(Decoder &decoder, const json &parent,
                                        const std::string &key, const std::string &path)
{
    ModelEndpointSchema result;
    const json *object = decoder.object(parent, key, path);
    if (object == nullptr)
        return result;
    decoder.unknownFields(*object, {"model", "endpoint", "api_key", "api_standard"}, path);
    result.model = decoder.string(*object, "model", path + ".model");
    result.endpoint = decoder.string(*object, "endpoint", path + ".endpoint");
    result.apiStandard = decoder.string(*object, "api_standard", path + ".api_standard");

    const bool anyFieldConfigured = !result.model.empty() || !result.endpoint.empty() ||
                                    !result.apiStandard.empty();
    if (!anyFieldConfigured)
        return result;
    if (!result.configured())
    {
        decoder.diagnostic(ConfigSeverity::FeatureDisabled, ConfigErrorCategory::Dependency,
                           path, "模型配置不完整，对应功能不可用");
        return result;
    }

    result.apiKey = decoder.secret(*object, "api_key", path + ".api_key",
                                   ConfigSeverity::FeatureDisabled);
    return result;
}

CommunicationSchema decodeCommunication(Decoder &decoder, const json &communication)
{
    CommunicationSchema result;
    decoder.unknownFields(communication,
                          {"protocol", "active_transport", "transports", "defaults"},
                          "communication");

    const json *protocol = decoder.object(communication, "protocol", "communication.protocol", true);
    if (protocol != nullptr)
    {
        decoder.unknownFields(*protocol, {"type", "options"}, "communication.protocol");
        result.protocolType = decoder.string(
            *protocol, "type", "communication.protocol.type", {}, true);
        if (!result.protocolType.empty() && result.protocolType != "onebot")
            decoder.diagnostic(ConfigSeverity::Fatal, ConfigErrorCategory::Dependency,
                               "communication.protocol.type", "当前版本仅支持 onebot");
    }

    result.activeTransport = decoder.string(
        communication, "active_transport", "communication.active_transport", {}, true);
    const json *transports = decoder.object(
        communication, "transports", "communication.transports", true);
    if (transports == nullptr || result.activeTransport.empty())
        return result;

    auto profileIterator = transports->find(result.activeTransport);
    if (profileIterator == transports->end() || !profileIterator->is_object())
    {
        decoder.diagnostic(ConfigSeverity::Fatal, ConfigErrorCategory::Missing,
                           "communication.transports." + result.activeTransport,
                           "活动传输配置不存在或不是对象");
        return result;
    }

    const json &profile = *profileIterator;
    const std::string profilePath = "communication.transports." + result.activeTransport;
    TransportProfileSchema &output = result.activeProfile;
    output.type = decoder.string(profile, "type", profilePath + ".type", {}, true);

    const json *defaults = decoder.object(communication, "defaults", "communication.defaults");
    const json empty = json::object();
    const json &defaultValues = defaults == nullptr ? empty : *defaults;
    if (defaults != nullptr)
        decoder.unknownFields(*defaults,
                              {"connect_timeout_ms", "request_timeout_ms", "max_event_body_bytes"},
                              "communication.defaults");
    result.connectTimeoutMs = decoder.integer(
        defaultValues, "connect_timeout_ms", "communication.defaults.connect_timeout_ms",
        5000, 1, 300000);
    result.requestTimeoutMs = decoder.integer(
        defaultValues, "request_timeout_ms", "communication.defaults.request_timeout_ms",
        15000, 1, 300000);
    result.maxBodyBytes = static_cast<std::size_t>(decoder.integer(
        defaultValues, "max_event_body_bytes", "communication.defaults.max_event_body_bytes",
        1048576, 1024, std::numeric_limits<int>::max()));

    if (output.type == "forward_websocket")
    {
        decoder.unknownFields(profile, {"type", "host", "port", "path", "access_token"}, profilePath);
        output.host = decoder.string(profile, "host", profilePath + ".host", {}, true);
        output.port = static_cast<unsigned short>(decoder.integer(
            profile, "port", profilePath + ".port", 0, 1, 65535, true));
        output.path = normalizedPath(
            decoder, profilePath + ".path",
            decoder.string(profile, "path", profilePath + ".path", "/"));
        output.accessToken = decoder.secret(profile, "access_token", profilePath + ".access_token");
    }
    else if (output.type == "reverse_websocket")
    {
        decoder.unknownFields(profile, {"type", "bind", "port", "path", "access_token"}, profilePath);
        output.host = decoder.string(profile, "bind", profilePath + ".bind", "127.0.0.1");
        output.port = static_cast<unsigned short>(decoder.integer(
            profile, "port", profilePath + ".port", 0, 1, 65535, true));
        output.path = normalizedPath(
            decoder, profilePath + ".path",
            decoder.string(profile, "path", profilePath + ".path", "/"));
        output.accessToken = decoder.secret(profile, "access_token", profilePath + ".access_token");
    }
    else if (output.type == "http")
    {
        decoder.unknownFields(profile, {"type", "api", "events"}, profilePath);
        const json *api = decoder.object(profile, "api", profilePath + ".api", true);
        const json *events = decoder.object(profile, "events", profilePath + ".events", true);
        if (api != nullptr)
        {
            decoder.unknownFields(*api, {"base_url", "access_token"}, profilePath + ".api");
            output.apiBaseUrl = decoder.string(
                *api, "base_url", profilePath + ".api.base_url", {}, true);
            output.apiAccessToken = decoder.secret(
                *api, "access_token", profilePath + ".api.access_token");
        }
        if (events != nullptr)
        {
            decoder.unknownFields(*events, {"bind", "port", "path", "access_token", "secret"},
                                  profilePath + ".events");
            output.eventBindHost = decoder.string(
                *events, "bind", profilePath + ".events.bind", "127.0.0.1");
            output.eventBindPort = static_cast<unsigned short>(decoder.integer(
                *events, "port", profilePath + ".events.port", 0, 1, 65535, true));
            output.eventPath = normalizedPath(
                decoder, profilePath + ".events.path",
                decoder.string(*events, "path", profilePath + ".events.path", "/onebot/events"));
            output.eventAccessToken = decoder.secret(
                *events, "access_token", profilePath + ".events.access_token");
            output.eventSecret = decoder.secret(
                *events, "secret", profilePath + ".events.secret");
        }
    }
    else if (!output.type.empty())
    {
        decoder.diagnostic(ConfigSeverity::Fatal, ConfigErrorCategory::Dependency,
                           profilePath + ".type", "未知传输类型");
    }

    return result;
}
}

std::string configSeverityName(ConfigSeverity severity)
{
    switch (severity)
    {
    case ConfigSeverity::Info: return "info";
    case ConfigSeverity::Warning: return "warning";
    case ConfigSeverity::FeatureDisabled: return "disabled";
    case ConfigSeverity::Error: return "error";
    case ConfigSeverity::Fatal: return "fatal";
    }
    return "unknown";
}

bool ConfigLoadResult::canStart() const
{
    if (!config)
        return false;
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const ConfigDiagnostic &diagnostic) {
        return diagnostic.severity == ConfigSeverity::Error ||
               diagnostic.severity == ConfigSeverity::Fatal;
    });
}

ConfigLoadResult ConfigLoader::loadFile(const std::string &path) const
{
    std::ifstream input(path);
    if (!input.is_open())
        return {nullptr, {{ConfigSeverity::Fatal, ConfigErrorCategory::Source, path, "配置文件无法打开"}}};
    std::ostringstream content;
    content << input.rdbuf();
    return loadText(content.str());
}

ConfigLoadResult ConfigLoader::loadText(const std::string &content) const
{
    try
    {
        return loadDocument(json::parse(content));
    }
    catch (const std::exception &error)
    {
        return {nullptr, {{ConfigSeverity::Fatal, ConfigErrorCategory::Syntax,
                           "$", "JSON 解析失败：" + std::string(error.what())}}};
    }
}

ConfigLoadResult ConfigLoader::loadDocument(const json &document) const
{
    ConfigLoadResult result;
    if (!document.is_object())
    {
        result.diagnostics.push_back({ConfigSeverity::Fatal, ConfigErrorCategory::Type,
                                      "$", "配置根节点必须是对象"});
        return result;
    }

    Decoder decoder(result.diagnostics);
    decoder.unknownFields(document,
                          {"schema_version", "bot", "chat", "models", "voice",
                           "memory", "web_search", "web_fetch", "storage",
                           "network", "communication", "webui"},
                          "$" );

    SchemaConfig config;
    config.schemaVersion = static_cast<int>(decoder.integer(
        document, "schema_version", "schema_version", 1, 1, 1, true));

    const json *bot = decoder.object(document, "bot", "bot", true);
    if (bot != nullptr)
    {
        decoder.unknownFields(*bot, {"id", "manager_id", "name", "group_chat_enabled"}, "bot");
        config.bot.id = decoder.unsignedInteger(*bot, "id", "bot.id", true);
        config.bot.managerId = decoder.unsignedInteger(*bot, "manager_id", "bot.manager_id", false);
        config.bot.name = decoder.string(*bot, "name", "bot.name", "Klein");
        config.bot.groupChatEnabled = decoder.boolean(*bot, "group_chat_enabled", "bot.group_chat_enabled", true);
    }

    const json *chat = decoder.object(document, "chat", "chat", true);
    if (chat != nullptr)
    {
        decoder.unknownFields(*chat,
                              {"default_model", "temperature", "top_p", "frequency_penalty",
                               "presence_penalty", "max_message_tokens", "worker_threads",
                               "message_survival_seconds"},
                              "chat");
        config.chat.defaultModel = decoder.string(*chat, "default_model", "chat.default_model", {}, true);
        config.chat.temperature = decoder.number(*chat, "temperature", "chat.temperature", 1.0, 0.0, 2.0);
        config.chat.topP = decoder.number(*chat, "top_p", "chat.top_p", 1.0, 0.0, 1.0);
        config.chat.frequencyPenalty = decoder.number(*chat, "frequency_penalty", "chat.frequency_penalty", 0.0, -2.0, 2.0);
        config.chat.presencePenalty = decoder.number(*chat, "presence_penalty", "chat.presence_penalty", 0.0, -2.0, 2.0);
        config.chat.maxMessageTokens = static_cast<std::size_t>(decoder.integer(
            *chat, "max_message_tokens", "chat.max_message_tokens", 4096, 1, 1000000));
        if (chat->contains("worker_threads"))
        {
            const long workerThreads = decoder.integer(
                *chat, "worker_threads", "chat.worker_threads", 0, 1, 256);
            if (workerThreads >= 1)
                config.chat.workerThreads = static_cast<std::size_t>(workerThreads);
        }
        config.chat.messageSurvivalSeconds = decoder.integer(
            *chat, "message_survival_seconds", "chat.message_survival_seconds", 3600, 1, std::numeric_limits<int>::max());
    }

    const json *models = decoder.object(document, "models", "models", true);
    if (models != nullptr)
    {
        // registry_path 已废弃（路径钉死为 kModelRegistryPath）：保留在白名单里
        // 让老配置静默加载，不弹“未知字段”警告
        decoder.unknownFields(*models, {"registry_path", "drawing", "vision", "stable_diffusion"}, "models");
        config.models.drawing = decodeModelEndpoint(decoder, *models, "drawing", "models.drawing");
        config.models.vision = decodeModelEndpoint(decoder, *models, "vision", "models.vision");
        const json *stable = decoder.object(*models, "stable_diffusion", "models.stable_diffusion");
        if (stable != nullptr)
        {
            decoder.unknownFields(*stable, {"endpoint", "model"}, "models.stable_diffusion");
            config.models.stableDiffusionEndpoint = decoder.string(*stable, "endpoint", "models.stable_diffusion.endpoint");
            config.models.stableDiffusionModel = decoder.string(*stable, "model", "models.stable_diffusion.model");
        }
    }

    const json *voice = decoder.object(document, "voice", "voice");
    if (voice != nullptr)
    {
        decoder.unknownFields(*voice, {"enabled", "host", "port", "reference_audio", "reference_text"}, "voice");
        config.voice.enabled = decoder.boolean(*voice, "enabled", "voice.enabled", false);
        config.voice.host = decoder.string(*voice, "host", "voice.host");
        config.voice.port = decoder.string(*voice, "port", "voice.port");
        config.voice.referenceAudioPath = decoder.string(*voice, "reference_audio", "voice.reference_audio");
        config.voice.referenceText = decoder.string(*voice, "reference_text", "voice.reference_text");
        if (config.voice.enabled && (config.voice.host.empty() || config.voice.port.empty()))
        {
            config.voice.enabled = false;
            decoder.diagnostic(ConfigSeverity::FeatureDisabled, ConfigErrorCategory::Dependency,
                               "voice", "语音配置不完整，语音功能已关闭");
        }
    }

    // features 段已废弃（accessibility_chat 开关移除，上下文模式仅管理员）：
    // 残留内容落入顶层 unknownFields 检查，仅提示后忽略

    const json *memory = decoder.object(document, "memory", "memory");
    if (memory != nullptr)
    {
        decoder.unknownFields(*memory, {"enabled", "model", "batch_turns", "idle_minutes", "recall_limit"}, "memory");
        config.memory.enabled = decoder.boolean(*memory, "enabled", "memory.enabled", true);
        config.memory.model = decoder.string(*memory, "model", "memory.model", config.chat.defaultModel);
        config.memory.batchTurns = static_cast<std::size_t>(decoder.integer(*memory, "batch_turns", "memory.batch_turns", 3, 1, 1000));
        config.memory.idleMinutes = static_cast<std::size_t>(decoder.integer(*memory, "idle_minutes", "memory.idle_minutes", 1, 1, 1440));
        config.memory.recallLimit = static_cast<std::size_t>(decoder.integer(*memory, "recall_limit", "memory.recall_limit", 8, 1, 1000));
    }
    else
    {
        config.memory.model = config.chat.defaultModel;
    }

    const json *webSearch = decoder.object(document, "web_search", "web_search");
    if (webSearch != nullptr)
    {
        decoder.unknownFields(
            *webSearch,
            {"enabled", "provider", "endpoint", "api_key", "search_depth",
             "max_results", "max_content_chars", "max_response_bytes",
             "connect_timeout_ms", "request_timeout_ms"},
            "web_search");
        config.webSearch.enabled = decoder.boolean(
            *webSearch, "enabled", "web_search.enabled", false);
        config.webSearch.provider = decoder.string(
            *webSearch, "provider", "web_search.provider", "tavily");
        config.webSearch.endpoint = decoder.string(
            *webSearch, "endpoint", "web_search.endpoint", "https://api.tavily.com/search");
        if (config.webSearch.enabled)
        {
            config.webSearch.apiKey = decoder.secret(
                *webSearch, "api_key", "web_search.api_key", ConfigSeverity::FeatureDisabled);
        }
        config.webSearch.searchDepth = decoder.string(
            *webSearch, "search_depth", "web_search.search_depth", "basic");
        config.webSearch.maxResults = static_cast<std::size_t>(decoder.integer(
            *webSearch, "max_results", "web_search.max_results", 5, 1, 10));
        config.webSearch.maxContentChars = static_cast<std::size_t>(decoder.integer(
            *webSearch, "max_content_chars", "web_search.max_content_chars", 2000, 100, 20000));
        config.webSearch.maxResponseBytes = static_cast<std::size_t>(decoder.integer(
            *webSearch, "max_response_bytes", "web_search.max_response_bytes",
            2097152, 1024, 16777216));
        config.webSearch.connectTimeoutMs = decoder.integer(
            *webSearch, "connect_timeout_ms", "web_search.connect_timeout_ms",
            5000, 100, 300000);
        config.webSearch.requestTimeoutMs = decoder.integer(
            *webSearch, "request_timeout_ms", "web_search.request_timeout_ms",
            15000, 100, 300000);
        if (config.webSearch.provider != "tavily")
        {
            config.webSearch.enabled = false;
            decoder.diagnostic(ConfigSeverity::FeatureDisabled, ConfigErrorCategory::Dependency,
                               "web_search.provider", "当前版本仅支持 tavily，联网搜索已关闭");
        }
        if (config.webSearch.searchDepth != "basic" &&
            config.webSearch.searchDepth != "advanced")
        {
            config.webSearch.searchDepth = "basic";
            decoder.diagnostic(ConfigSeverity::Warning, ConfigErrorCategory::Range,
                               "web_search.search_depth", "仅支持 basic/advanced，已使用 basic");
        }
        if (config.webSearch.enabled && config.webSearch.apiKey.empty())
        {
            config.webSearch.enabled = false;
            decoder.diagnostic(ConfigSeverity::FeatureDisabled, ConfigErrorCategory::Security,
                               "web_search.api_key", "API Key 缺失，联网搜索已关闭");
        }
    }

    const json *webFetch = decoder.object(document, "web_fetch", "web_fetch");
    if (webFetch != nullptr)
    {
        decoder.unknownFields(
            *webFetch,
            {"enabled", "max_content_chars", "max_response_bytes",
             "connect_timeout_ms", "request_timeout_ms", "cache_ttl_seconds",
             "cache_max_entries"},
            "web_fetch");
        config.webFetch.enabled = decoder.boolean(
            *webFetch, "enabled", "web_fetch.enabled", false);
        config.webFetch.maxContentChars = static_cast<std::size_t>(decoder.integer(
            *webFetch, "max_content_chars", "web_fetch.max_content_chars",
            12000, 500, 60000));
        config.webFetch.maxResponseBytes = static_cast<std::size_t>(decoder.integer(
            *webFetch, "max_response_bytes", "web_fetch.max_response_bytes",
            2097152, 1024, 16777216));
        config.webFetch.connectTimeoutMs = decoder.integer(
            *webFetch, "connect_timeout_ms", "web_fetch.connect_timeout_ms",
            5000, 100, 300000);
        config.webFetch.requestTimeoutMs = decoder.integer(
            *webFetch, "request_timeout_ms", "web_fetch.request_timeout_ms",
            20000, 100, 300000);
        config.webFetch.cacheTtlSeconds = decoder.integer(
            *webFetch, "cache_ttl_seconds", "web_fetch.cache_ttl_seconds",
            900, 0, 86400);
        config.webFetch.cacheMaxEntries = static_cast<std::size_t>(decoder.integer(
            *webFetch, "cache_max_entries", "web_fetch.cache_max_entries",
            32, 1, 1024));
    }

    const json *storage = decoder.object(document, "storage", "storage");
    if (storage != nullptr)
    {
        decoder.unknownFields(*storage, {"conversation_database", "image_assets"}, "storage");
        config.storage.conversationDatabase = decoder.string(*storage, "conversation_database", "storage.conversation_database", "source/conversations.db");
        config.storage.imageAssets = decoder.string(*storage, "image_assets", "storage.image_assets", "source/image_assets");
    }

    // resources 段已废弃（help 内置、图片下载功能移除）：残留内容落入顶层
    // unknownFields 检查，仅提示后忽略

    const json *network = decoder.object(document, "network", "network");
    if (network != nullptr)
    {
        decoder.unknownFields(*network, {"proxy"}, "network");
        config.proxy = decoder.string(*network, "proxy", "network.proxy");
    }

    const json *communication = decoder.object(document, "communication", "communication", true);
    if (communication != nullptr)
        config.communication = decodeCommunication(decoder, *communication);

    const json *webUi = decoder.object(document, "webui", "webui");
    if (webUi != nullptr)
    {
        decoder.unknownFields(*webUi, {"enabled", "bind", "port", "access_token"}, "webui");
        config.webUi.enabled = decoder.boolean(*webUi, "enabled", "webui.enabled", false);
        config.webUi.bind = decoder.string(*webUi, "bind", "webui.bind", "127.0.0.1");
        config.webUi.port = static_cast<int>(decoder.integer(
            *webUi, "port", "webui.port", kDefaultWebUiPort, 1, 65535));
        if (config.webUi.enabled)
            config.webUi.accessToken = decoder.secret(
                *webUi, "access_token", "webui.access_token", ConfigSeverity::FeatureDisabled);
        if (config.webUi.enabled && config.webUi.accessToken.empty())
        {
            config.webUi.enabled = false;
            decoder.diagnostic(ConfigSeverity::FeatureDisabled, ConfigErrorCategory::Security,
                               "webui.access_token", "访问令牌缺失，Web 配置面板已关闭");
        }
        if (config.webUi.enabled && config.webUi.bind != "127.0.0.1" &&
            config.webUi.bind != "localhost" && config.webUi.bind != "::1")
        {
            decoder.diagnostic(ConfigSeverity::Warning, ConfigErrorCategory::Security,
                               "webui.bind",
                               "绑定地址不是回环地址，面板将暴露给外部网络，请确认已由反向代理提供 TLS");
        }
    }

    result.config = std::make_shared<const SchemaConfig>(std::move(config));
    return result;
}
