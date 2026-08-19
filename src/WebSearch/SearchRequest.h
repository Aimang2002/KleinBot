#ifndef SEARCH_REQUEST_H
#define SEARCH_REQUEST_H

#include <cstddef>
#include <optional>
#include <string>

struct SearchRequest
{
    std::string query;
    std::size_t maxResults = 5;
    std::string topic = "general";
    std::optional<std::string> timeRange;
    std::optional<int> days;
};

#endif // SEARCH_REQUEST_H
