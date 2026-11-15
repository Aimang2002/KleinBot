#ifndef WEB_FETCH_ACTION_H
#define WEB_FETCH_ACTION_H

#include "Action.h"
#include "WebToolGuidance.h"
#include "../WebFetch/WebFetchOptions.h"
#include "../utils/Live.h"
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

    // options：组合根的活值（ADR-023），每次执行开头取一份快照，传给下面的缓存与抓取。
    // 缓存按插入时的配置算过期时间、按当前上限淘汰
    WebFetchAction(const Live<WebFetchOptions> &options, const std::atomic<bool> *running = nullptr,
                   HttpGet httpGet = {}, Distiller distiller = {});

    const ActionDescriptor &descriptor() const override;
    ActionResult execute(const nlohmann::json &arguments,
                         const ActionContext &context) override;
    // web_fetch.enabled 是活值：关掉后工具仍注册，只是不对模型开放（ADR-023 稳定外壳）
    bool available() const override { return options.get()->enabled; }
    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &) const override
    {
        return webFetchUsageGuidance(scene);
    }

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

    bool lookupCache(const WebFetchOptions &current, const std::string &key, FetchedPage &page);
    void storeCache(const WebFetchOptions &current, const std::string &key, const FetchedPage &page);
    void loadPage(const WebFetchOptions &current, const std::string &url, FetchedPage &page);
    PageHttpResponse performGet(const WebFetchOptions &current, const std::string &url);

    const Live<WebFetchOptions> &options;
    const std::atomic<bool> *running;
    HttpGet httpGet;
    Distiller distiller;
    std::mutex cacheMutex;
    std::unordered_map<std::string, CacheEntry> cache;
};

#endif // WEB_FETCH_ACTION_H
