#ifndef WEB_FETCH_ACTION_H
#define WEB_FETCH_ACTION_H

#include "Action.h"
#include "../WebFetch/WebFetchOptions.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

struct PageHttpResponse
{
    long statusCode = 0;
    std::string contentType;
    std::string effectiveUrl;
    std::string body;
    bool exceededSizeLimit = false;
};

class WebFetchAction : public Action
{
public:
    using HttpGet = std::function<PageHttpResponse(const std::string &url)>;
    // 把提取提示词交给廉价模型蒸馏；返回空串表示蒸馏不可用，由调用方降级
    using Distiller = std::function<std::string(const std::string &prompt)>;

    WebFetchAction(WebFetchOptions options, const std::atomic<bool> *running = nullptr,
                   HttpGet httpGet = {}, Distiller distiller = {});

    const ActionDescriptor &descriptor() const override;
    ActionResult execute(const nlohmann::json &arguments,
                         const ActionContext &context) override;

private:
    struct FetchedPage
    {
        long statusCode = 0;
        std::string effectiveUrl;
        std::string contentType;
        std::string charset;
        std::string title;
        std::string text;
        std::size_t bodyBytes = 0;
        bool downloadTruncated = false;
    };

    struct CacheEntry
    {
        FetchedPage page;
        std::chrono::steady_clock::time_point expires;
        std::chrono::steady_clock::time_point lastAccess;
    };

    bool lookupCache(const std::string &key, FetchedPage &page);
    void storeCache(const std::string &key, const FetchedPage &page);
    void loadPage(const std::string &url, FetchedPage &page);
    PageHttpResponse performGet(const std::string &url);

    WebFetchOptions options;
    const std::atomic<bool> *running;
    HttpGet httpGet;
    Distiller distiller;
    std::mutex cacheMutex;
    std::unordered_map<std::string, CacheEntry> cache;
};

#endif // WEB_FETCH_ACTION_H
