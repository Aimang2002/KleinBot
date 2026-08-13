#include "TextRecall.h"
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace
{
std::string normalizeText(const std::string &value)
{
    std::string normalized;
    normalized.reserve(value.size());
    bool previousSpace = true;
    for (std::size_t index = 0; index < value.size();)
    {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character < 0x80)
        {
            if (std::isalnum(character))
            {
                normalized.push_back(static_cast<char>(std::tolower(character)));
                previousSpace = false;
            }
            else if (!previousSpace)
            {
                normalized.push_back(' ');
                previousSpace = true;
            }
            ++index;
            continue;
        }

        std::size_t length = 1;
        if ((character & 0xE0) == 0xC0)
            length = 2;
        else if ((character & 0xF0) == 0xE0)
            length = 3;
        else if ((character & 0xF8) == 0xF0)
            length = 4;
        length = std::min(length, value.size() - index);
        normalized.append(value, index, length);
        previousSpace = false;
        index += length;
    }
    while (!normalized.empty() && normalized.back() == ' ')
        normalized.pop_back();
    return normalized;
}

std::vector<std::string> splitWords(const std::string &value)
{
    std::vector<std::string> words;
    std::size_t start = 0;
    while (start < value.size())
    {
        while (start < value.size() && value[start] == ' ')
            ++start;
        if (start == value.size())
            break;
        const std::size_t end = value.find(' ', start);
        words.push_back(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return words;
}

std::vector<std::string> utf8Characters(const std::string &value)
{
    std::vector<std::string> characters;
    for (std::size_t index = 0; index < value.size();)
    {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        std::size_t length = 1;
        if ((character & 0xE0) == 0xC0)
            length = 2;
        else if ((character & 0xF0) == 0xE0)
            length = 3;
        else if ((character & 0xF8) == 0xF0)
            length = 4;
        length = std::min(length, value.size() - index);
        characters.push_back(value.substr(index, length));
        index += length;
    }
    return characters;
}

bool hasNonAscii(const std::string &value)
{
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return character >= 0x80;
    });
}

bool isStopTerm(const std::string &term)
{
    static const std::unordered_set<std::string> stopTerms = {
        "我", "你", "他", "她", "它", "我们", "你们", "他们", "之前", "以前",
        "曾经", "现在", "原来", "最早", "是不是", "有没有", "是否", "什么",
        "哪个", "多少", "怎么", "为什么", "一下", "关于", "提过", "说过",
        "告诉", "记得", "事情", "东西", "问题", "这个", "那个"};
    return stopTerms.find(term) != stopTerms.end();
}

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
