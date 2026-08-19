#include "HtmlTextExtractor.h"
#include <array>
#include <cctype>
#include <unordered_map>

namespace
{
std::string toLower(std::string value)
{
    for (char &character : value)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}

// 整块丢弃的标签：出现开标签时跳到对应闭标签为止
bool isDroppedTag(const std::string &name)
{
    static const std::array<const char *, 15> dropped{
        "script", "style", "noscript", "template", "svg", "iframe", "object",
        "embed", "canvas", "nav", "aside", "footer", "form", "button", "select"};
    for (const char *tag : dropped)
    {
        if (name == tag)
            return true;
    }
    return false;
}

bool isBlockTag(const std::string &name)
{
    static const std::array<const char *, 24> blocks{
        "p", "div", "section", "article", "main", "ul", "ol", "dl", "dt", "dd",
        "table", "thead", "tbody", "tfoot", "tr", "blockquote", "pre", "figure",
        "figcaption", "address", "details", "summary", "center", "fieldset"};
    for (const char *tag : blocks)
    {
        if (name == tag)
            return true;
    }
    return false;
}

bool isHeadingTag(const std::string &name)
{
    return name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
}

void appendUtf8(std::string &output, unsigned int codepoint)
{
    if (codepoint > 0x10FFFF)
        codepoint = 0xFFFD;
    if (codepoint < 0x80)
    {
        output += static_cast<char>(codepoint);
    }
    else if (codepoint < 0x800)
    {
        output += static_cast<char>(0xC0 | (codepoint >> 6));
        output += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    else if (codepoint < 0x10000)
    {
        output += static_cast<char>(0xE0 | (codepoint >> 12));
        output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    else
    {
        output += static_cast<char>(0xF0 | (codepoint >> 18));
        output += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

std::string decodeHtmlEntities(const std::string &input)
{
    static const std::unordered_map<std::string, const char *> named{
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"},
        {"nbsp", "\xC2\xA0"}, {"copy", "\xC2\xA9"}, {"reg", "\xC2\xAE"},
        {"trade", "\xE2\x84\xA2"}, {"hellip", "\xE2\x80\xA6"},
        {"mdash", "\xE2\x80\x94"}, {"ndash", "\xE2\x80\x93"},
        {"lsquo", "\xE2\x80\x98"}, {"rsquo", "\xE2\x80\x99"},
        {"ldquo", "\xE2\x80\x9C"}, {"rdquo", "\xE2\x80\x9D"},
        {"bull", "\xE2\x80\xA2"}, {"middot", "\xC2\xB7"}, {"deg", "\xC2\xB0"},
        {"plusmn", "\xC2\xB1"}, {"times", "\xC3\x97"}, {"divide", "\xC3\xB7"},
        {"euro", "\xE2\x82\xAC"}, {"pound", "\xC2\xA3"}, {"yen", "\xC2\xA5"}};

    std::string output;
    output.reserve(input.size());
    std::size_t index = 0;
    while (index < input.size())
    {
        if (input[index] != '&')
        {
            output += input[index++];
            continue;
        }
        const std::size_t semicolon = input.find(';', index + 1);
        if (semicolon == std::string::npos || semicolon - index > 12)
        {
            output += input[index++];
            continue;
        }
        const std::string entity = input.substr(index + 1, semicolon - index - 1);
        const auto namedMatch = named.find(toLower(entity));
        if (namedMatch != named.end())
        {
            output += namedMatch->second;
            index = semicolon + 1;
            continue;
        }
        unsigned int codepoint = 0;
        bool numeric = false;
        std::string value = entity;
        if (!value.empty() && value[0] == '#')
        {
            numeric = true;
            value.erase(0, 1);
        }
        if (numeric && !value.empty() && (value[0] == 'x' || value[0] == 'X'))
        {
            value.erase(0, 1);
            for (char character : value)
            {
                if (!std::isxdigit(static_cast<unsigned char>(character)))
                {
                    numeric = false;
                    break;
                }
                const int digit = character <= '9'
                                      ? character - '0'
                                      : (character | 0x20) - 'a' + 10;
                codepoint = codepoint * 16 + static_cast<unsigned int>(digit);
            }
        }
        else if (numeric)
        {
            for (char character : value)
            {
                if (!std::isdigit(static_cast<unsigned char>(character)))
                {
                    numeric = false;
                    break;
                }
                codepoint = codepoint * 10 + static_cast<unsigned int>(character - '0');
            }
        }
        if (numeric && codepoint != 0)
        {
            appendUtf8(output, codepoint);
            index = semicolon + 1;
        }
        else
        {
            output += input[index++];
        }
    }
    return output;
}

void collapseAsciiWhitespace(std::string &value)
{
    std::size_t write = 0;
    bool pendingSpace = false;
    for (std::size_t read = 0; read < value.size(); ++read)
    {
        const unsigned char character = static_cast<unsigned char>(value[read]);
        const bool isSpace = character == ' ' || character == '\t' || character == '\r' ||
                             character == '\n' || character == '\f' || character == '\v';
        if (isSpace)
        {
            pendingSpace = write > 0;
            continue;
        }
        if (pendingSpace && value[write - 1] != ' ')
            value[write++] = ' ';
        pendingSpace = false;
        value[write++] = value[read];
    }
    value.resize(write);
}

void breakLine(std::string &output)
{
    if (!output.empty() && output.back() != '\n')
        output += '\n';
}

void ensureBlankLine(std::string &output)
{
    breakLine(output);
    if (output.size() >= 2 && output[output.size() - 2] != '\n')
        output += '\n';
}

std::size_t skipDroppedTag(const std::string &html, std::size_t from,
                           const std::string &name)
{
    const std::string closer = "</" + name;
    const std::size_t match = html.find(closer, from);
    if (match == std::string::npos)
        return html.size();
    std::size_t after = match + closer.size();
    while (after < html.size() && html[after] != '>')
        ++after;
    return after < html.size() ? after + 1 : html.size();
}
}

std::string detectHtmlCharset(const std::string &html)
{
    const std::string head = html.substr(0, std::min<std::size_t>(html.size(), 4096));
    const std::string lowered = toLower(head);
    const std::size_t marker = lowered.find("charset");
    if (marker == std::string::npos)
        return {};
    std::size_t cursor = marker + 7;
    while (cursor < lowered.size() &&
           (lowered[cursor] == ' ' || lowered[cursor] == '=' ||
            lowered[cursor] == '"' || lowered[cursor] == '\''))
        ++cursor;
    std::string charset;
    while (cursor < lowered.size() && charset.size() < 24 &&
           (std::isalnum(static_cast<unsigned char>(lowered[cursor])) ||
            lowered[cursor] == '-'))
    {
        charset += lowered[cursor];
        ++cursor;
    }
    return charset;
}

HtmlTextExtraction extractHtmlText(const std::string &html)
{
    HtmlTextExtraction result;
    std::string &output = result.text;
    bool capturingTitle = false;

    auto appendText = [&](const std::string &raw)
    {
        std::string decoded = decodeHtmlEntities(raw);
        collapseAsciiWhitespace(decoded);
        if (decoded.empty())
            return;
        if (capturingTitle)
        {
            result.title += decoded;
            return;
        }
        if (!output.empty() && output.back() == '\n')
        {
            std::size_t leading = decoded.find_first_not_of(' ');
            if (leading == std::string::npos)
                return;
            decoded.erase(0, leading);
        }
        output += decoded;
    };

    std::size_t index = 0;
    while (index < html.size())
    {
        if (html[index] != '<')
        {
            const std::size_t start = index;
            while (index < html.size() && html[index] != '<')
                ++index;
            appendText(html.substr(start, index - start));
            continue;
        }
        if (html.compare(index, 4, "<!--") == 0)
        {
            const std::size_t end = html.find("-->", index + 4);
            index = end == std::string::npos ? html.size() : end + 3;
            continue;
        }
        if (index + 1 < html.size() && (html[index + 1] == '!' || html[index + 1] == '?'))
        {
            const std::size_t end = html.find('>', index + 1);
            index = end == std::string::npos ? html.size() : end + 1;
            continue;
        }

        const bool closing = index + 1 < html.size() && html[index + 1] == '/';
        std::size_t cursor = index + 1 + (closing ? 1 : 0);
        const std::size_t nameStart = cursor;
        while (cursor < html.size() &&
               (std::isalpha(static_cast<unsigned char>(html[cursor])) ||
                (cursor > nameStart &&
                 std::isdigit(static_cast<unsigned char>(html[cursor])))))
            ++cursor;
        const std::string name = toLower(html.substr(nameStart, cursor - nameStart));
        const std::size_t tagEnd = html.find('>', cursor);
        if (tagEnd == std::string::npos)
            break;
        const bool selfClosing = tagEnd > cursor && html[tagEnd - 1] == '/';

        if (name == "title")
        {
            if (!closing && !selfClosing)
                capturingTitle = true;
            else if (closing)
            {
                capturingTitle = false;
                collapseAsciiWhitespace(result.title);
            }
        }
        else if (!closing && isDroppedTag(name) && !selfClosing)
        {
            if (capturingTitle)
                capturingTitle = false;
            index = skipDroppedTag(html, tagEnd + 1, name);
            continue;
        }
        else if (name == "br")
        {
            if (!capturingTitle)
                breakLine(output);
        }
        else if (name == "hr")
        {
            if (!capturingTitle)
            {
                ensureBlankLine(output);
                output += "----\n";
            }
        }
        else if (isHeadingTag(name))
        {
            if (capturingTitle)
                continue;
            if (!closing)
            {
                ensureBlankLine(output);
                output += std::string(name[1] - '0', '#') + " ";
            }
            else
                breakLine(output);
        }
        else if (name == "li")
        {
            if (capturingTitle)
                continue;
            if (!closing)
            {
                breakLine(output);
                output += "- ";
            }
            else
                breakLine(output);
        }
        else if (name == "td" || name == "th")
        {
            if (!capturingTitle)
            {
                if (!closing && !output.empty() && output.back() != '\n' && output.back() != ' ')
                    output += ' ';
                else if (closing)
                    output += ' ';
            }
        }
        else if (isBlockTag(name))
        {
            if (!capturingTitle)
                breakLine(output);
        }
        index = tagEnd + 1;
    }

    while (!output.empty() && (output.back() == '\n' || output.back() == ' '))
        output.pop_back();
    while (!result.title.empty() && result.title.back() == ' ')
        result.title.pop_back();
    return result;
}
