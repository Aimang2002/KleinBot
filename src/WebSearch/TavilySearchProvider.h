#ifndef TAVILY_SEARCH_PROVIDER_H
#define TAVILY_SEARCH_PROVIDER_H

#include "SearchProvider.h"
#include "WebSearchOptions.h"
#include "../utils/Live.h"
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

    // options：组合根的活值（ADR-023），每次搜索开头取一份快照，改了下一次搜索即用新值
    TavilySearchProvider(const Live<WebSearchOptions> &options,
                         const std::atomic<bool> *running = nullptr,
                         HttpPost httpPost = {});

    SearchResponse search(const SearchRequest &request) override;
private:
    TavilyHttpResponse performPost(const WebSearchOptions &current,
                                   const std::string &endpoint,
                                   const std::string &apiKey,
                                   const std::string &payload) const;

    const Live<WebSearchOptions> &options;
    const std::atomic<bool> *running;
    HttpPost httpPost;
};

#endif // TAVILY_SEARCH_PROVIDER_H
