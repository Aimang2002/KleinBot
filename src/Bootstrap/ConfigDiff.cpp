#include "ConfigDiff.h"

#include <algorithm>
#include <utility>

namespace
{
    template <typename Value>
    void addIfChanged(ConfigDiff &diff, const Value &current, const Value &candidate,
                      const std::string &path, ConfigChangeImpact impact)
    {
        if (current != candidate)
            diff.add(path, impact);
    }

    void compareModelEndpoint(ConfigDiff &diff, const ModelEndpointSchema &current,
                              const ModelEndpointSchema &candidate, const std::string &path)
    {
        addIfChanged(diff, current.model, candidate.model,
                     path + ".model", ConfigChangeImpact::Rebuild);
        addIfChanged(diff, current.endpoint, candidate.endpoint,
                     path + ".endpoint", ConfigChangeImpact::Rebuild);
        addIfChanged(diff, current.apiKey, candidate.apiKey,
                     path + ".api_key", ConfigChangeImpact::Rebuild);
        addIfChanged(diff, current.apiStandard, candidate.apiStandard,
                     path + ".api_standard", ConfigChangeImpact::Rebuild);
    }

    void compareTransportProfile(ConfigDiff &diff, const TransportProfileSchema &current,
                                 const TransportProfileSchema &candidate)
    {
        constexpr ConfigChangeImpact impact = ConfigChangeImpact::Rebuild;
        addIfChanged(diff, current.type, candidate.type, "communication.active_profile.type", impact);
        addIfChanged(diff, current.host, candidate.host, "communication.active_profile.host", impact);
        addIfChanged(diff, current.port, candidate.port, "communication.active_profile.port", impact);
        addIfChanged(diff, current.path, candidate.path, "communication.active_profile.path", impact);
        addIfChanged(diff, current.accessToken, candidate.accessToken,
                     "communication.active_profile.access_token", impact);
        addIfChanged(diff, current.apiBaseUrl, candidate.apiBaseUrl,
                     "communication.active_profile.api.base_url", impact);
        addIfChanged(diff, current.apiAccessToken, candidate.apiAccessToken,
                     "communication.active_profile.api.access_token", impact);
        addIfChanged(diff, current.eventBindHost, candidate.eventBindHost,
                     "communication.active_profile.events.bind", impact);
        addIfChanged(diff, current.eventBindPort, candidate.eventBindPort,
                     "communication.active_profile.events.port", impact);
        addIfChanged(diff, current.eventPath, candidate.eventPath,
                     "communication.active_profile.events.path", impact);
        addIfChanged(diff, current.eventAccessToken, candidate.eventAccessToken,
                     "communication.active_profile.events.access_token", impact);
        addIfChanged(diff, current.eventSecret, candidate.eventSecret,
                     "communication.active_profile.events.secret", impact);
    }
}

bool ConfigDiff::empty() const
{
    return entries.empty();
}

std::size_t ConfigDiff::size() const
{
    return entries.size();
}

std::size_t ConfigDiff::count(ConfigChangeImpact impact) const
{
    return static_cast<std::size_t>(std::count_if(
        entries.begin(), entries.end(), [impact](const ConfigChange &change)
        { return change.impact == impact; }));
}

const std::vector<ConfigChange> &ConfigDiff::changes() const
{
    return entries;
}

void ConfigDiff::add(std::string path, ConfigChangeImpact impact)
{
    entries.push_back({std::move(path), impact});
}

ConfigDiff compareConfig(const SchemaConfig &current, const SchemaConfig &candidate)
{
    ConfigDiff diff;
    addIfChanged(diff, current.schemaVersion, candidate.schemaVersion,
                 "schema_version", ConfigChangeImpact::Restart);

    addIfChanged(diff, current.bot.id, candidate.bot.id, "bot.id", ConfigChangeImpact::Restart);
    addIfChanged(diff, current.bot.managerId, candidate.bot.managerId,
                 "bot.manager_id", ConfigChangeImpact::Restart);
    addIfChanged(diff, current.bot.name, candidate.bot.name,
                 "bot.name", ConfigChangeImpact::Restart);
    addIfChanged(diff, current.bot.groupChatEnabled, candidate.bot.groupChatEnabled,
                 "bot.group_chat_enabled", ConfigChangeImpact::Dynamic);

    constexpr ConfigChangeImpact rebuild = ConfigChangeImpact::Rebuild;
    addIfChanged(diff, current.chat.defaultModel, candidate.chat.defaultModel,
                 "chat.default_model", rebuild);
    addIfChanged(diff, current.chat.temperature, candidate.chat.temperature,
                 "chat.temperature", rebuild);
    addIfChanged(diff, current.chat.topP, candidate.chat.topP, "chat.top_p", rebuild);
    addIfChanged(diff, current.chat.frequencyPenalty, candidate.chat.frequencyPenalty,
                 "chat.frequency_penalty", rebuild);
    addIfChanged(diff, current.chat.presencePenalty, candidate.chat.presencePenalty,
                 "chat.presence_penalty", rebuild);
    addIfChanged(diff, current.chat.maxMessageTokens, candidate.chat.maxMessageTokens,
                 "chat.max_message_tokens", rebuild);
    addIfChanged(diff, current.chat.workerThreads, candidate.chat.workerThreads,
                 "chat.worker_threads", ConfigChangeImpact::Restart);
    addIfChanged(diff, current.chat.messageSurvivalSeconds, candidate.chat.messageSurvivalSeconds,
                 "chat.message_survival_seconds", rebuild);

    addIfChanged(diff, current.models.registryPath, candidate.models.registryPath,
                 "models.registry_path", ConfigChangeImpact::Restart);
    compareModelEndpoint(diff, current.models.drawing, candidate.models.drawing, "models.drawing");
    compareModelEndpoint(diff, current.models.vision, candidate.models.vision, "models.vision");
    addIfChanged(diff, current.models.stableDiffusionEndpoint,
                 candidate.models.stableDiffusionEndpoint,
                 "models.stable_diffusion.endpoint", rebuild);
    addIfChanged(diff, current.models.stableDiffusionModel,
                 candidate.models.stableDiffusionModel,
                 "models.stable_diffusion.model", rebuild);

    addIfChanged(diff, current.voice.enabled, candidate.voice.enabled,
                 "voice.enabled", ConfigChangeImpact::Dynamic);
    addIfChanged(diff, current.voice.host, candidate.voice.host, "voice.host", rebuild);
    addIfChanged(diff, current.voice.port, candidate.voice.port, "voice.port", rebuild);
    addIfChanged(diff, current.voice.referenceAudioPath, candidate.voice.referenceAudioPath,
                 "voice.reference_audio_path", rebuild);
    addIfChanged(diff, current.voice.referenceText, candidate.voice.referenceText,
                 "voice.reference_text", rebuild);

    addIfChanged(diff, current.memory.enabled, candidate.memory.enabled, "memory.enabled", rebuild);
    addIfChanged(diff, current.memory.model, candidate.memory.model, "memory.model", rebuild);
    addIfChanged(diff, current.memory.batchTurns, candidate.memory.batchTurns,
                 "memory.batch_turns", rebuild);
    addIfChanged(diff, current.memory.idleMinutes, candidate.memory.idleMinutes,
                 "memory.idle_minutes", rebuild);
    addIfChanged(diff, current.memory.recallLimit, candidate.memory.recallLimit,
                 "memory.recall_limit", rebuild);

    addIfChanged(diff, current.webSearch.enabled, candidate.webSearch.enabled,
                 "web_search.enabled", rebuild);
    addIfChanged(diff, current.webSearch.provider, candidate.webSearch.provider,
                 "web_search.provider", rebuild);
    addIfChanged(diff, current.webSearch.endpoint, candidate.webSearch.endpoint,
                 "web_search.endpoint", rebuild);
    addIfChanged(diff, current.webSearch.apiKey, candidate.webSearch.apiKey,
                 "web_search.api_key", rebuild);
    addIfChanged(diff, current.webSearch.searchDepth, candidate.webSearch.searchDepth,
                 "web_search.search_depth", rebuild);
    addIfChanged(diff, current.webSearch.maxResults, candidate.webSearch.maxResults,
                 "web_search.max_results", rebuild);
    addIfChanged(diff, current.webSearch.maxContentChars, candidate.webSearch.maxContentChars,
                 "web_search.max_content_chars", rebuild);
    addIfChanged(diff, current.webSearch.maxResponseBytes, candidate.webSearch.maxResponseBytes,
                 "web_search.max_response_bytes", rebuild);
    addIfChanged(diff, current.webSearch.connectTimeoutMs, candidate.webSearch.connectTimeoutMs,
                 "web_search.connect_timeout_ms", rebuild);
    addIfChanged(diff, current.webSearch.requestTimeoutMs, candidate.webSearch.requestTimeoutMs,
                 "web_search.request_timeout_ms", rebuild);

    addIfChanged(diff, current.webFetch.enabled, candidate.webFetch.enabled,
                 "web_fetch.enabled", rebuild);
    addIfChanged(diff, current.webFetch.maxContentChars, candidate.webFetch.maxContentChars,
                 "web_fetch.max_content_chars", rebuild);
    addIfChanged(diff, current.webFetch.maxResponseBytes, candidate.webFetch.maxResponseBytes,
                 "web_fetch.max_response_bytes", rebuild);
    addIfChanged(diff, current.webFetch.connectTimeoutMs, candidate.webFetch.connectTimeoutMs,
                 "web_fetch.connect_timeout_ms", rebuild);
    addIfChanged(diff, current.webFetch.requestTimeoutMs, candidate.webFetch.requestTimeoutMs,
                 "web_fetch.request_timeout_ms", rebuild);
    addIfChanged(diff, current.webFetch.cacheTtlSeconds, candidate.webFetch.cacheTtlSeconds,
                 "web_fetch.cache_ttl_seconds", rebuild);
    addIfChanged(diff, current.webFetch.cacheMaxEntries, candidate.webFetch.cacheMaxEntries,
                 "web_fetch.cache_max_entries", rebuild);

    addIfChanged(diff, current.storage.conversationDatabase,
                 candidate.storage.conversationDatabase,
                 "storage.conversation_database", ConfigChangeImpact::Restart);
    addIfChanged(diff, current.storage.imageAssets, candidate.storage.imageAssets,
                 "storage.image_assets", ConfigChangeImpact::Restart);

    addIfChanged(diff, current.proxy, candidate.proxy, "network.proxy", rebuild);

    addIfChanged(diff, current.communication.protocolType,
                 candidate.communication.protocolType,
                 "communication.protocol.type", rebuild);
    addIfChanged(diff, current.communication.activeTransport,
                 candidate.communication.activeTransport,
                 "communication.active_transport", rebuild);
    compareTransportProfile(diff, current.communication.activeProfile,
                            candidate.communication.activeProfile);
    addIfChanged(diff, current.communication.connectTimeoutMs,
                 candidate.communication.connectTimeoutMs,
                 "communication.defaults.connect_timeout_ms", rebuild);
    addIfChanged(diff, current.communication.requestTimeoutMs,
                 candidate.communication.requestTimeoutMs,
                 "communication.defaults.request_timeout_ms", rebuild);
    addIfChanged(diff, current.communication.maxBodyBytes,
                 candidate.communication.maxBodyBytes,
                 "communication.defaults.max_event_body_bytes", rebuild);

    constexpr ConfigChangeImpact webUiImpact = ConfigChangeImpact::Restart;
    addIfChanged(diff, current.webUi.enabled, candidate.webUi.enabled,
                 "webui.enabled", webUiImpact);
    addIfChanged(diff, current.webUi.bind, candidate.webUi.bind, "webui.bind", webUiImpact);
    addIfChanged(diff, current.webUi.port, candidate.webUi.port, "webui.port", webUiImpact);
    addIfChanged(diff, current.webUi.accessToken, candidate.webUi.accessToken,
                 "webui.access_token", webUiImpact);
    return diff;
}

std::string configChangeImpactName(ConfigChangeImpact impact)
{
    switch (impact)
    {
    case ConfigChangeImpact::Dynamic:
        return "dynamic";
    case ConfigChangeImpact::Rebuild:
        return "rebuild";
    case ConfigChangeImpact::Restart:
        return "restart";
    }
    return "unknown";
}
