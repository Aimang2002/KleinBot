#ifndef SEARCH_PROVIDER_H
#define SEARCH_PROVIDER_H

#include "SearchRequest.h"
#include "SearchResult.h"
#include <string>
#include <vector>

struct SearchResponse
{
    std::vector<SearchResult> results;
    // Provider 附带的综合摘要（如 Tavily 的 include_answer），可为空；
    // 作为第一份参考证据交给模型，不替代逐条结果
    std::string answer;
};

class SearchProvider
{
public:
    virtual ~SearchProvider() = default;
    virtual SearchResponse search(const SearchRequest &request) = 0;
};

#endif // SEARCH_PROVIDER_H
