#include "HttpApiChannel.h"
#include "../Log/Log.h"

#include <curl/curl.h>

#include <algorithm>

namespace
{
std::string actionUrl(const std::string &baseUrl, const std::string &action)
{
    if (!baseUrl.empty() && baseUrl.back() == '/')
        return baseUrl + action;
    return baseUrl + "/" + action;
}

size_t appendResponse(char *data, size_t size, size_t count, void *output)
{
    auto *response = static_cast<std::string *>(output);
    response->append(data, size * count);
    return size * count;
}

int cancelWhenStopped(void *runningPointer, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const auto *running = static_cast<const std::atomic<bool> *>(runningPointer);
    return running != nullptr && running->load() ? 0 : 1;
}
}

HttpApiChannel::HttpApiChannel(std::string apiBaseUrl, std::string apiAuthToken,
                               long connectTimeoutMs, long requestTimeoutMs,
                               const std::atomic<bool> *running)
    : apiBaseUrl(std::move(apiBaseUrl)), apiAuthToken(std::move(apiAuthToken)),
      connectTimeoutMs(connectTimeoutMs), requestTimeoutMs(requestTimeoutMs), running(running)
{
}

OneBotApiResult HttpApiChannel::call(const std::string &action, nlohmann::json params,
                                     std::chrono::milliseconds timeout)
{
    OneBotApiResult result;
    params["echo"] = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    result.echo = params.value("echo", 0LL);
    const std::string requestBody = params.dump();
    std::string responseBody;

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl)
    {
        LOG_ERROR("OneBot HTTP API 通道 curl 初始化失败");
        result.networkError = true;
        return result;
    }

    curl_slist *rawHeaders = nullptr;
    rawHeaders = curl_slist_append(rawHeaders, "Content-Type: application/json");
    if (!apiAuthToken.empty())
    {
        const std::string authorization = "Authorization: Bearer " + apiAuthToken;
        rawHeaders = curl_slist_append(rawHeaders, authorization.c_str());
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(rawHeaders,
                                                                        curl_slist_free_all);

    // 调用方超时与传输配置取小者：任何一方都不该被对方无限拖住
    const long totalTimeoutMs = std::min<long>(
        requestTimeoutMs,
        std::max<long>(1L, static_cast<long>(
                              std::chrono::duration_cast<std::chrono::milliseconds>(timeout)
                                  .count())));

    curl_easy_setopt(curl.get(), CURLOPT_URL, actionUrl(apiBaseUrl, action).c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, requestBody.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, requestBody.size());
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, connectTimeoutMs);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, totalTimeoutMs);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, appendResponse);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &responseBody);
    if (running != nullptr)
    {
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, cancelWhenStopped);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, running);
    }

    const CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK)
    {
        LOG_ERROR("OneBot HTTP API 通道请求失败：" + std::string(curl_easy_strerror(code)));
        result.networkError = true;
        return result;
    }

    long statusCode = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &statusCode);
    if (statusCode < 200 || statusCode >= 300)
    {
        LOG_ERROR("OneBot HTTP API 通道返回状态码：" + std::to_string(statusCode));
        result.networkError = true;
        return result;
    }

    try
    {
        const nlohmann::json document = nlohmann::json::parse(responseBody);
        result.status = document.value("status", "");
        result.retcode = document.value("retcode", 0LL);
        if (document.contains("data"))
            result.data = document["data"];
        if (document.contains("echo"))
            result.echo = document.value("echo", result.echo);
    }
    catch (const std::exception &error)
    {
        LOG_ERROR("OneBot HTTP API 通道响应解析失败：" + std::string(error.what()));
        result.networkError = true;
    }
    return result;
}
