#ifndef CURL_REQUEST_CONTROL_H
#define CURL_REQUEST_CONTROL_H

#include <atomic>
#include <curl/curl.h>

namespace CurlRequestControl
{
constexpr long kDefaultTimeoutMs = 300000;

void configure(CURL *curl, const std::atomic<bool> *running,
               long connectTimeoutMs = kDefaultTimeoutMs, long requestTimeoutMs = kDefaultTimeoutMs);
bool cancellationRequested(const std::atomic<bool> *running);
bool wasCancelled(CURLcode result, const std::atomic<bool> *running);
long failureStatusCode(CURLcode result);
const char *failureMessage(CURLcode result);

// 本机协议端通道（OneBot HTTP API 调用与出站投递）不走任何代理：显式把代理设成空串，libcurl 就不再读
// http_proxy / https_proxy / all_proxy 这些环境变量。部署者的终端常带着桌面代理变量，而 libcurl 7.81
// 不认 no_proxy 里的 127.0.0.0/8 网段写法，会把发往 127.0.0.1 的请求交给代理；代理没开时就是
// “Couldn't connect to server”，开着时是代理回的 503（2026-11-20、11-23 两次真机）。
// 协议端没有自己的代理配置项；模型、联网搜索与网页抓取仍按 network.proxy 与环境变量走，不受影响
void bypassProxy(CURL *curl);
}

#endif
