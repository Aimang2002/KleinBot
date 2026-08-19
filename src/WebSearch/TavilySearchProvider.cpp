#include "TavilySearchProvider.h"
#include "../Network/CurlRequestControl.h"
#include "../../Library/nlohmann/json.hpp"
#include <algorithm>
#include <curl/curl.h>
#include <stdexcept>
#include <utility>

namespace
{
struct ResponseBuffer
{
    std::string content;
    std::size_t limit = 0;
    bool exceeded = false;
};

std::size_t appendResponse(char *data, std::size_t size, std::size_t count, void *userData)
{
    auto *buffer = static_cast<ResponseBuffer *>(userData);
    const std::size_t bytes = size * count;
    if (bytes > buffer->limit - buffer->content.size())
    {
        buffer->exceeded = true;
        return 0;
    }
    buffer->content.append(data, bytes);
    return bytes;
}

std::string apiErrorMessage(const nlohmann::json &document)
{
    const auto detail = document.find("detail");
    if (detail != document.end())
    {
        if (detail->is_string())
            return detail->get<std::string>();
        if (detail->is_object())
        {
            const auto error = detail->find("error");
            if (error != detail->end() && error->is_string())
                return error->get<std::string>();
        }
    }
    return "搜索服务返回错误";
}

std::vector<SearchResult> parseResults(const nlohmann::json &document,
                                       std::size_t maxResults)
{
    const auto resultArray = document.find("results");
    if (resultArray == document.end() || !resultArray->is_array())
        throw std::runtime_error("搜索服务响应缺少 results 数组");

    std::vector<SearchResult> results;
    results.reserve(std::min(maxResults, resultArray->size()));
    for (const auto &item : *resultArray)
    {
        if (results.size() >= maxResults)
            break;
        if (!item.is_object())
            continue;
        const auto title = item.find("title");
        const auto url = item.find("url");
        const auto content = item.find("content");
        if (title == item.end() || !title->is_string() ||
            url == item.end() || !url->is_string() ||
            content == item.end() || !content->is_string())
            continue;

        SearchResult result;
        result.title = title->get<std::string>();
        result.url = url->get<std::string>();
        result.content = content->get<std::string>();
        const auto published = item.find("published_date");
        if (published != item.end() && published->is_string())
            result.publishedAt = published->get<std::string>();
        const auto score = item.find("score");
        if (score != item.end() && score->is_number())
            result.score = score->get<double>();
        results.push_back(std::move(result));
    }
    return results;
}
}

TavilySearchProvider::TavilySearchProvider(WebSearchOptions options,
                                           const std::atomic<bool> *running,
                                           HttpPost httpPost)
    : options(std::move(options)), running(running), httpPost(std::move(httpPost))
{
}

SearchResponse TavilySearchProvider::search(const SearchRequest &request)
{
    nlohmann::json payload{
        {"query", request.query},
        {"max_results", request.maxResults},
        {"search_depth", options.searchDepth},
        {"include_answer", true},
        {"include_raw_content", false}};
    if (!request.topic.empty() && request.topic != "general")
        payload["topic"] = request.topic;
    if (request.timeRange.has_value())
        payload["time_range"] = *request.timeRange;
    if (request.days.has_value())
        payload["days"] = *request.days;

    const TavilyHttpResponse response = httpPost
        ? httpPost(options.endpoint, options.apiKey, payload.dump())
        : performPost(options.endpoint, options.apiKey, payload.dump());

    nlohmann::json document;
    try
    {
        document = nlohmann::json::parse(response.body);
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error("搜索服务返回了无效 JSON：" + std::string(error.what()));
    }

    if (response.statusCode < 200 || response.statusCode >= 300)
        throw std::runtime_error(apiErrorMessage(document));

    SearchResponse output;
    output.results = parseResults(document, request.maxResults);
    const auto answer = document.find("answer");
    if (answer != document.end() && answer->is_string())
        output.answer = answer->get<std::string>();
    return output;
}

TavilyHttpResponse TavilySearchProvider::performPost(const std::string &endpoint,
                                                     const std::string &apiKey,
                                                     const std::string &payload) const
{
    CURL *curl = curl_easy_init();
    if (curl == nullptr)
        throw std::runtime_error("无法初始化搜索网络请求");

    ResponseBuffer responseBuffer;
    responseBuffer.limit = options.maxResponseBytes;
    curl_slist *headers = nullptr;
    const std::string authorization = "Authorization: Bearer " + apiKey;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, authorization.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendResponse);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "KleinBot/2.4 web-search");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    CurlRequestControl::configure(
        curl, running, options.connectTimeoutMs, options.requestTimeoutMs);
    if (!options.proxy.empty())
        curl_easy_setopt(curl, CURLOPT_PROXY, options.proxy.c_str());

    const CURLcode result = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (responseBuffer.exceeded)
        throw std::runtime_error("搜索服务响应超过大小限制");
    if (CurlRequestControl::wasCancelled(result, running))
        throw std::runtime_error("搜索请求已取消");
    if (result != CURLE_OK)
        throw std::runtime_error(CurlRequestControl::failureMessage(result));
    return {statusCode, std::move(responseBuffer.content)};
}
