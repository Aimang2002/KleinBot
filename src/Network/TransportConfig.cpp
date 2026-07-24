#include "TransportConfig.h"

#include "../ConfigManager/ConfigManager.h"

#include <limits>
#include <stdexcept>

namespace
{
std::string getValue(
    const std::unordered_map<std::string, std::string> &values,
    const std::string &key,
    const std::string &fallbackKey = "",
    const std::string &defaultValue = "")
{
    auto value = values.find(key);
    if (value != values.end() && !value->second.empty())
    {
        return value->second;
    }
    if (!fallbackKey.empty())
    {
        value = values.find(fallbackKey);
        if (value != values.end() && !value->second.empty())
        {
            return value->second;
        }
    }
    return defaultValue;
}

long parsePositiveLong(const std::string &name, const std::string &value)
{
    std::size_t parsedLength = 0;
    const long parsed = std::stol(value, &parsedLength);
    if (parsedLength != value.size() || parsed <= 0)
    {
        throw std::invalid_argument(name + " must be a positive integer");
    }
    return parsed;
}

unsigned short parsePort(const std::string &name, const std::string &value, bool required)
{
    if (value.empty() && !required)
    {
        return 0;
    }

    const long parsed = parsePositiveLong(name, value);
    if (parsed > std::numeric_limits<unsigned short>::max())
    {
        throw std::invalid_argument(name + " exceeds 65535");
    }
    return static_cast<unsigned short>(parsed);
}

std::size_t parseSize(const std::string &name, const std::string &value)
{
    const long parsed = parsePositiveLong(name, value);
    return static_cast<std::size_t>(parsed);
}

std::string normalizePath(const std::string &name, std::string path)
{
    if (path.empty())
    {
        return "/";
    }
    if (path.front() != '/')
    {
        throw std::invalid_argument(name + " must start with '/'");
    }
    return path;
}

TransportMode parseMode(const std::string &value)
{
    if (value == "forward_websocket")
    {
        return TransportMode::ForwardWebSocket;
    }
    if (value == "reverse_websocket")
    {
        return TransportMode::ReverseWebSocket;
    }
    if (value == "http")
    {
        return TransportMode::Http;
    }
    throw std::invalid_argument(
        "TRANSPORT_MODE must be forward_websocket, reverse_websocket or http");
}
}

TransportConfig TransportConfig::fromConfigManager()
{
    ConfigManager &config = ConfigManager::getInstance();
    std::unordered_map<std::string, std::string> values;
    for (const std::string &key : {
             "TRANSPORT_MODE", "WS_EVENT_HOST", "WS_EVENT_PORT", "WS_EVENT_PATH",
             "WS_API_BIND_HOST", "WS_API_BIND_PORT", "WS_API_PATH",
             "WEBSOCKET_AUTH_TOKEN", "WEBSOCKET_MESSAGE_IP", "WEBSOCKET_MESSAGE_PORT",
             "REVERSEWEBSOCKET_MESSAGE_IP", "REVERSEWEBSOCKET_MESSAGE_PORT",
             "HTTP_API_BASE_URL", "HTTP_API_AUTH_TOKEN", "HTTP_EVENT_BIND_HOST",
             "HTTP_EVENT_BIND_PORT", "HTTP_EVENT_PATH", "HTTP_EVENT_AUTH_TOKEN",
             "NETWORK_CONNECT_TIMEOUT_MS", "NETWORK_REQUEST_TIMEOUT_MS",
             "NETWORK_MAX_BODY_BYTES"})
    {
        values.emplace(key, config.configVariableOpt(key));
    }
    return fromValues(values);
}

TransportConfig TransportConfig::fromValues(
    const std::unordered_map<std::string, std::string> &values)
{
    TransportConfig config;
    config.mode = parseMode(getValue(values, "TRANSPORT_MODE", "", "reverse_websocket"));

    config.forwardWebSocket.host = getValue(values, "WS_EVENT_HOST", "WEBSOCKET_MESSAGE_IP");
    config.forwardWebSocket.port = getValue(values, "WS_EVENT_PORT", "WEBSOCKET_MESSAGE_PORT");
    config.forwardWebSocket.path = normalizePath(
        "WS_EVENT_PATH", getValue(values, "WS_EVENT_PATH", "", "/"));
    config.forwardWebSocket.authToken = getValue(values, "WEBSOCKET_AUTH_TOKEN");

    config.reverseWebSocket.bindHost = getValue(
        values, "WS_API_BIND_HOST", "REVERSEWEBSOCKET_MESSAGE_IP");
    config.reverseWebSocket.bindPort = parsePort(
        "WS_API_BIND_PORT",
        getValue(values, "WS_API_BIND_PORT", "REVERSEWEBSOCKET_MESSAGE_PORT"),
        config.mode == TransportMode::ReverseWebSocket);
    config.reverseWebSocket.path = normalizePath(
        "WS_API_PATH", getValue(values, "WS_API_PATH", "", "/"));
    config.reverseWebSocket.authToken = getValue(values, "WEBSOCKET_AUTH_TOKEN");

    config.http.apiBaseUrl = getValue(values, "HTTP_API_BASE_URL");
    config.http.apiAuthToken = getValue(values, "HTTP_API_AUTH_TOKEN");
    config.http.eventBindHost = getValue(
        values, "HTTP_EVENT_BIND_HOST", "", "127.0.0.1");
    config.http.eventBindPort = parsePort(
        "HTTP_EVENT_BIND_PORT", getValue(values, "HTTP_EVENT_BIND_PORT"),
        config.mode == TransportMode::Http);
    config.http.eventPath = normalizePath(
        "HTTP_EVENT_PATH", getValue(values, "HTTP_EVENT_PATH", "", "/onebot/events"));
    config.http.eventAuthToken = getValue(values, "HTTP_EVENT_AUTH_TOKEN");

    config.connectTimeoutMs = parsePositiveLong(
        "NETWORK_CONNECT_TIMEOUT_MS",
        getValue(values, "NETWORK_CONNECT_TIMEOUT_MS", "", "5000"));
    config.requestTimeoutMs = parsePositiveLong(
        "NETWORK_REQUEST_TIMEOUT_MS",
        getValue(values, "NETWORK_REQUEST_TIMEOUT_MS", "", "15000"));
    config.maxBodyBytes = parseSize(
        "NETWORK_MAX_BODY_BYTES",
        getValue(values, "NETWORK_MAX_BODY_BYTES", "", "1048576"));

    if (config.mode == TransportMode::ForwardWebSocket &&
        (config.forwardWebSocket.host.empty() || config.forwardWebSocket.port.empty()))
    {
        throw std::invalid_argument("forward_websocket requires WS_EVENT_HOST and WS_EVENT_PORT");
    }
    if (config.mode == TransportMode::ReverseWebSocket && config.reverseWebSocket.bindHost.empty())
    {
        throw std::invalid_argument("reverse_websocket requires WS_API_BIND_HOST");
    }
    if (config.mode == TransportMode::Http && config.http.apiBaseUrl.empty())
    {
        throw std::invalid_argument("http requires HTTP_API_BASE_URL");
    }

    return config;
}

std::string transportModeName(TransportMode mode)
{
    switch (mode)
    {
    case TransportMode::ForwardWebSocket:
        return "forward_websocket";
    case TransportMode::ReverseWebSocket:
        return "reverse_websocket";
    case TransportMode::Http:
        return "http";
    }
    return "unknown";
}
