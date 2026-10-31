#ifndef WEB_FETCH_OPTIONS_H
#define WEB_FETCH_OPTIONS_H

#include <cstddef>
#include <string>

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
