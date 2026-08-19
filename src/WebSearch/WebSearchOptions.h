#ifndef WEB_SEARCH_OPTIONS_H
#define WEB_SEARCH_OPTIONS_H

#include <cstddef>
#include <string>

struct WebSearchOptions
{
    bool enabled = false;
    std::string provider = "tavily";
    std::string endpoint = "https://api.tavily.com/search";
    std::string apiKey;
    std::string proxy;
    std::string searchDepth = "basic";
    std::size_t maxResults = 5;
    std::size_t maxContentChars = 2000;
    std::size_t maxResponseBytes = 2097152;
    long connectTimeoutMs = 5000;
    long requestTimeoutMs = 15000;
};

#endif // WEB_SEARCH_OPTIONS_H
