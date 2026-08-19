#ifndef TAVILY_SEARCH_PROVIDER_H
#define TAVILY_SEARCH_PROVIDER_H

#include "SearchProvider.h"
#include "WebSearchOptions.h"
#include <atomic>
#include <functional>
#include <string>

struct TavilyHttpResponse
{
    long statusCode = 0;
    std::string body;
};

class TavilySearchProvider : public SearchProvider
{
public:
    using HttpPost = std::function<TavilyHttpResponse(
        const std::string &endpoint, const std::string &apiKey,
        const std::string &payload)>;

    TavilySearchProvider(WebSearchOptions options,
                         const std::atomic<bool> *running = nullptr,
                         HttpPost httpPost = {});

    SearchResponse search(const SearchRequest &request) override;
private:
    TavilyHttpResponse performPost(const std::string &endpoint,
                                   const std::string &apiKey,
                                   const std::string &payload) const;

    WebSearchOptions options;
    const std::atomic<bool> *running;
    HttpPost httpPost;
};

#endif // TAVILY_SEARCH_PROVIDER_H
