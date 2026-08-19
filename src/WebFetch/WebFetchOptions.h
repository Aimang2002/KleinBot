#ifndef WEB_FETCH_OPTIONS_H
#define WEB_FETCH_OPTIONS_H

#include <cstddef>
#include <string>

// 与 klein_web_search 同前缀，避免第三方网关抢占保留名 web_fetch
inline constexpr const char *KleinWebFetchToolName = "klein_web_fetch";

struct WebFetchOptions
{
    bool enabled = false;
    std::size_t maxContentChars = 12000;
    std::size_t maxResponseBytes = 2097152;
    long connectTimeoutMs = 5000;
    long requestTimeoutMs = 20000;
    long cacheTtlSeconds = 900;
    std::size_t cacheMaxEntries = 32;
    std::string proxy;
};

#endif // WEB_FETCH_OPTIONS_H
