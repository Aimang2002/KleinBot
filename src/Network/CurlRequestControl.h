#ifndef CURL_REQUEST_CONTROL_H
#define CURL_REQUEST_CONTROL_H

#include <atomic>
#include <curl/curl.h>

namespace CurlRequestControl
{
void configure(CURL *curl, const std::atomic<bool> *running,
               long connectTimeoutMs = 10000, long requestTimeoutMs = 120000);
bool cancellationRequested(const std::atomic<bool> *running);
bool wasCancelled(CURLcode result, const std::atomic<bool> *running);
long failureStatusCode(CURLcode result);
const char *failureMessage(CURLcode result);
}

#endif
