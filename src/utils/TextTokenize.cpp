#include "TextTokenize.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace utils
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
}
