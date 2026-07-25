#ifndef TRANSPORT_CONFIG_H
#define TRANSPORT_CONFIG_H

#include <cstddef>
#include <string>

enum class TransportMode
{
    ForwardWebSocket,
    ReverseWebSocket,
    Http
};

struct ForwardWebSocketConfig
{
    std::string host;
    std::string port;
    std::string path = "/";
    std::string authToken;
};

struct ReverseWebSocketConfig
{
    std::string bindHost;
    unsigned short bindPort = 0;
    std::string path = "/";
    std::string authToken;
};

struct HttpTransportConfig
{
    std::string apiBaseUrl;
    std::string apiAuthToken;
    std::string eventBindHost = "127.0.0.1";
    unsigned short eventBindPort = 0;
    std::string eventPath = "/onebot/events";
    std::string eventAuthToken;
};

struct TransportConfig
{
    TransportMode mode = TransportMode::ReverseWebSocket;
    ForwardWebSocketConfig forwardWebSocket;
    ReverseWebSocketConfig reverseWebSocket;
    HttpTransportConfig http;
    long connectTimeoutMs = 5000;
    long requestTimeoutMs = 15000;
    std::size_t maxBodyBytes = 1048576;
};

std::string transportModeName(TransportMode mode);

#endif
