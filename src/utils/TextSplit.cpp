#include "TextSplit.h"

#include <algorithm>
#include <cctype>

namespace
{
std::string trimLine(const std::string &line)
{
    auto isSpace = [](unsigned char c)
    { return c == ' ' || c == '\t' || c == '\r'; };
    std::size_t begin = 0;
    std::size_t end = line.size();
    while (begin < end && isSpace(static_cast<unsigned char>(line[begin])))
        ++begin;
    while (end > begin && isSpace(static_cast<unsigned char>(line[end - 1])))
        --end;
    return line.substr(begin, end - begin);
}

bool isFence(const std::string &trimmed)
{
    return trimmed.rfind("```", 0) == 0;
}

// UTF-8 感知硬切：按字符数切段，绝不截断到半个多字节字符
void hardSplitUtf8(const std::string &segment, std::size_t maxChars,
                   std::vector<std::string> &out)
{
    if (segment.size() <= maxChars)
    {
        out.push_back(segment);
        return;
    }
    std::size_t start = 0;
    while (start < segment.size())
    {
        std::size_t charCount = 0;
        std::size_t pos = start;
        while (pos < segment.size() && charCount < maxChars)
        {
            unsigned char lead = static_cast<unsigned char>(segment[pos]);
            std::size_t len = 1;
            if ((lead & 0xF8) == 0xF0)
                len = 4;
            else if ((lead & 0xF0) == 0xE0)
                len = 3;
            else if ((lead & 0xE0) == 0xC0)
                len = 2;
            if (pos + len > segment.size())
                len = segment.size() - pos;
            pos += len;
            ++charCount;
        }
        out.push_back(segment.substr(start, pos - start));
        start = pos;
    }
}
} // namespace

std::vector<std::string> splitTextSegments(const std::string &text,
                                           std::size_t maxChars,
                                           std::size_t maxSegments)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, newline - start));
        start = newline + 1;
        if (start == text.size())
            break;
    }

    std::vector<std::string> segments;
    bool inCode = false;
    std::string codeBuffer;
    auto emit = [&](const std::string &segment)
    {
        if (!segment.empty())
            segments.push_back(segment);
    };

    for (const std::string &raw : lines)
    {
        const std::string trimmed = trimLine(raw);
        if (inCode)
        {
            if (isFence(trimmed))
            {
                codeBuffer += trimmed;
                emit(codeBuffer);
                codeBuffer.clear();
                inCode = false;
            }
            else
            {
                codeBuffer += raw;
                codeBuffer += '\n';
            }
            continue;
        }
        if (isFence(trimmed))
        {
            inCode = true;
            codeBuffer = trimmed + '\n';
            continue;
        }
        emit(trimmed);
    }
    if (inCode && !codeBuffer.empty())
    {
        // 未闭合围栏：按已积累内容发送（去掉缓冲产生的尾随换行）
        while (!codeBuffer.empty() && codeBuffer.back() == '\n')
            codeBuffer.pop_back();
        emit(codeBuffer);
    }

    if (segments.size() > maxSegments)
    {
        std::string overflow = segments[maxSegments - 1];
        for (std::size_t index = maxSegments; index < segments.size(); ++index)
        {
            overflow += '\n';
            overflow += segments[index];
        }
        segments.resize(maxSegments);
        segments[maxSegments - 1] = overflow;
    }

    std::vector<std::string> result;
    for (const std::string &segment : segments)
        hardSplitUtf8(segment, maxChars, result);
    return result;
}
