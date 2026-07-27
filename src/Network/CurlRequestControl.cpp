#include "CurlRequestControl.h"

namespace
{
int cancelWhenStopped(void *runningPointer, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const auto *running = static_cast<const std::atomic<bool> *>(runningPointer);
    return running != nullptr && !running->load() ? 1 : 0;
}
}

void CurlRequestControl::configure(CURL *curl, const std::atomic<bool> *running,
                                   long connectTimeoutMs, long requestTimeoutMs)
{
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connectTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, requestTimeoutMs);
    if (running != nullptr)
    {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, cancelWhenStopped);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, running);
    }
}

bool CurlRequestControl::cancellationRequested(const std::atomic<bool> *running)
{
    return running != nullptr && !running->load();
}

bool CurlRequestControl::wasCancelled(CURLcode result, const std::atomic<bool> *running)
{
    return result == CURLE_ABORTED_BY_CALLBACK || cancellationRequested(running);
}

long CurlRequestControl::failureStatusCode(CURLcode result)
{
    return result == CURLE_OPERATION_TIMEDOUT ? 504L : 503L;
}

const char *CurlRequestControl::failureMessage(CURLcode result)
{
    return result == CURLE_OPERATION_TIMEDOUT
        ? "模型请求超时，请稍后重试。"
        : "无法连接模型服务，请稍后重试。";
}
