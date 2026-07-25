#include "RuntimeSettings.h"

namespace
{
    ModelEndpointOptions mapModel(const ModelEndpointSchema &schema)
    {
        return {schema.model, schema.endpoint, schema.apiKey, schema.apiStandard};
    }

    TransportConfig mapTransport(const CommunicationSchema &communication)
    {
        TransportConfig result;
        result.connectTimeoutMs = communication.connectTimeoutMs;
        result.requestTimeoutMs = communication.requestTimeoutMs;
        result.maxBodyBytes = communication.maxBodyBytes;

        const TransportProfileSchema &profile = communication.activeProfile;
        if (profile.type == "forward_websocket")
        {
            result.mode = TransportMode::ForwardWebSocket;
            result.forwardWebSocket.host = profile.host;
            result.forwardWebSocket.port = std::to_string(profile.port);
            result.forwardWebSocket.path = profile.path;
            result.forwardWebSocket.authToken = profile.accessToken;
        }
        else if (profile.type == "reverse_websocket")
        {
            result.mode = TransportMode::ReverseWebSocket;
            result.reverseWebSocket.bindHost = profile.host;
            result.reverseWebSocket.bindPort = profile.port;
            result.reverseWebSocket.path = profile.path;
            result.reverseWebSocket.authToken = profile.accessToken;
        }
        else
        {
            result.mode = TransportMode::Http;
            result.http.apiBaseUrl = profile.apiBaseUrl;
            result.http.apiAuthToken = profile.apiAccessToken;
            result.http.eventBindHost = profile.eventBindHost;
            result.http.eventBindPort = profile.eventBindPort;
            result.http.eventPath = profile.eventPath;
            result.http.eventAuthToken = profile.eventAccessToken;
        }
        return result;
    }
}

RuntimeSettings buildRuntimeSettings(const SchemaConfig &schema)
{
    RuntimeSettings result;
    result.schemaVersion = schema.schemaVersion;
    result.bot = {schema.bot.id, schema.bot.managerId, schema.bot.name};
    result.chat = {
        schema.chat.defaultModel,
        schema.chat.temperature,
        schema.chat.topP,
        schema.chat.frequencyPenalty,
        schema.chat.presencePenalty,
        schema.chat.maxMessageTokens,
        schema.chat.workerThreads,
        schema.chat.messageSurvivalSeconds,
        schema.chat.privateAction,
        schema.chat.groupAction};
    result.models.registryPath = schema.models.registryPath;
    result.models.drawing = mapModel(schema.models.drawing);
    result.models.vision = mapModel(schema.models.vision);
    result.models.stableDiffusionEndpoint = schema.models.stableDiffusionEndpoint;
    result.models.stableDiffusionModel = schema.models.stableDiffusionModel;
    result.voice = {
        schema.voice.enabled,
        schema.voice.host,
        schema.voice.port,
        schema.voice.outputDirectory,
        schema.voice.referenceAudioPath,
        schema.voice.referenceText};
    result.memory = {
        schema.memory.enabled,
        schema.memory.model,
        schema.memory.batchTurns,
        schema.memory.idleSeconds,
        schema.memory.recallLimit};
    result.message = {
        result.bot,
        schema.bot.groupChatEnabled};
    result.dock.proxy = schema.proxy;
    result.storage = {schema.storage.conversationDatabase, schema.storage.imageAssets};
    result.resources = {
        schema.resources.personalityDirectory,
        schema.resources.helpFile,
        schema.resources.imageDownloadDirectory};
    result.transport = mapTransport(schema.communication);
    return result;
}
