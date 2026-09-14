#include "TextRecall.h"
#include "../utils/TextTokenize.h"
#include <algorithm>
#include <unordered_map>

namespace
{
using utils::hasNonAscii;
using utils::isStopTerm;
using utils::normalizeText;
using utils::splitWords;
using utils::utf8Characters;

void addTerm(std::unordered_map<std::string, double> &terms, const std::string &term,
             double weight)
{
    if (term.empty() || isStopTerm(term))
        return;
    auto iterator = terms.find(term);
    if (iterator == terms.end() || iterator->second < weight)
        terms[term] = weight;
}
}

RecallQueryPlan buildRecallQueryPlan(const std::vector<std::string> &queries,
                                     std::size_t maxTerms)
{
    RecallQueryPlan plan;
    std::unordered_map<std::string, double> terms;
    for (const auto &query : queries)
    {
        const std::string normalized = normalizeText(query);
        if (normalized.empty())
            continue;
        if (std::find(plan.phrases.begin(), plan.phrases.end(), normalized) == plan.phrases.end())
            plan.phrases.push_back(normalized);
        addTerm(terms, normalized, 6.0);

        for (const auto &word : splitWords(normalized))
        {
            addTerm(terms, word, 4.0);
            if (!hasNonAscii(word))
                continue;

            const auto characters = utf8Characters(word);
            for (std::size_t gramSize : {std::size_t(3), std::size_t(2)})
            {
                if (characters.size() < gramSize)
                    continue;
                for (std::size_t start = 0; start + gramSize <= characters.size(); ++start)
                {
                    std::string gram;
                    for (std::size_t offset = 0; offset < gramSize; ++offset)
                        gram += characters[start + offset];
                    addTerm(terms, gram, gramSize == 3 ? 2.5 : 1.5);
                }
            }
        }
    }

    plan.terms.reserve(terms.size());
    for (const auto &entry : terms)
        plan.terms.push_back({entry.first, entry.second});
    std::sort(plan.terms.begin(), plan.terms.end(), [](const RecallTerm &left,
                                                       const RecallTerm &right) {
        if (left.weight != right.weight)
            return left.weight > right.weight;
        if (left.text.size() != right.text.size())
            return left.text.size() > right.text.size();
        return left.text < right.text;
    });
    if (plan.terms.size() > maxTerms)
        plan.terms.resize(maxTerms);
    return plan;
}

double scoreRecallText(const RecallQueryPlan &plan, const std::string &text)
{
    const std::string normalized = normalizeText(text);
    if (normalized.empty())
        return 0.0;

    double score = 0.0;
    for (const auto &phrase : plan.phrases)
    {
        if (normalized == phrase)
            score += 14.0;
        else if (normalized.find(phrase) != std::string::npos)
            score += 9.0;
    }

    std::size_t matchedTerms = 0;
    double matchedWeight = 0.0;
    double totalWeight = 0.0;
    for (const auto &term : plan.terms)
    {
        totalWeight += term.weight;
        if (normalized.find(term.text) == std::string::npos)
            continue;
        ++matchedTerms;
        matchedWeight += term.weight;
    }
    score += matchedWeight;
    if (!plan.terms.empty())
        score += 4.0 * static_cast<double>(matchedTerms) /
                 static_cast<double>(plan.terms.size());
    if (totalWeight > 0.0)
        score += 3.0 * matchedWeight / totalWeight;
    return score;
}
