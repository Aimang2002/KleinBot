#ifndef TEXT_RECALL_H
#define TEXT_RECALL_H

#include <cstddef>
#include <string>
#include <vector>

struct RecallTerm
{
    std::string text;
    double weight = 0.0;
};

struct RecallQueryPlan
{
    std::vector<std::string> phrases;
    std::vector<RecallTerm> terms;
};

RecallQueryPlan buildRecallQueryPlan(const std::vector<std::string> &queries,
                                     std::size_t maxTerms = 24);
double scoreRecallText(const RecallQueryPlan &plan, const std::string &text);

#endif // TEXT_RECALL_H
