#ifndef SEARCH_RESULT_H
#define SEARCH_RESULT_H

#include <optional>
#include <string>

struct SearchResult
{
    std::string title;
    std::string url;
    std::string content;
    std::optional<std::string> publishedAt;
    std::optional<double> score;
};

#endif // SEARCH_RESULT_H
