#include "ReplyCheck.h"

#include "../utils/Utils.hpp"

#include <regex>

namespace affect
{
namespace
{
// 档位护栏（资源护栏，代码常量）：只防失控，宽松
constexpr std::size_t kShortSentences = 1;
constexpr std::size_t kOneSentences = 1;
constexpr std::size_t kFewSentences = 3;
constexpr std::size_t kShortCodepoints = 24;
constexpr std::size_t kOneCodepoints = 60;
constexpr std::size_t kFewCodepoints = 150;

// 从 pos 起的一个 UTF-8 字符（不合法的字节按单字节算）
std::string nextCodepoint(const std::string &text, std::size_t &pos)
{
    const unsigned char lead = static_cast<unsigned char>(text[pos]);
    std::size_t length = 1;
    if ((lead & 0xF8) == 0xF0)
        length = 4;
    else if ((lead & 0xF0) == 0xE0)
        length = 3;
    else if ((lead & 0xE0) == 0xC0)
        length = 2;
    if (pos + length > text.size())
        length = 1;
    std::string unit = text.substr(pos, length);
    pos += length;
    return unit;
}

bool isOneOf(const std::string &unit, std::initializer_list<const char *> values)
{
    for (const char *value : values)
        if (unit == value)
            return true;
    return false;
}

bool isTerminator(const std::string &unit)
{
    return isOneOf(unit, {"。", "！", "？", "!", "?", "\n"});
}

bool isBlank(const std::string &unit)
{
    return isOneOf(unit, {" ", "\t", "\r", "\v", "\f", "　"});
}

// 句内与句末的标点：只有标点的一段不算一句
bool isPunctuation(const std::string &unit)
{
    return isOneOf(unit, {"，", "、", "；", "：", "…", "～", "~", "—", "-", "·", ",", ".", ";",
                          ":", "《", "》", "〈", "〉", "「", "」", "『", "』", "“", "”", "‘",
                          "’", "（", "）", "(", ")", "【", "】", "[", "]", "\"", "'"});
}

std::string leftTrimmed(const std::string &line)
{
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
        ++start;
    return line.substr(start);
}

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// 行首的标题 / 引用 / 无序列表记号的字节长度（含其后的空格）；没有返回 0
std::size_t blockMarkerLength(const std::string &line)
{
    std::size_t hashes = 0;
    while (hashes < line.size() && line[hashes] == '#')
        ++hashes;
    if (hashes >= 1 && hashes <= 6 && hashes < line.size() && line[hashes] == ' ')
        return hashes + 1;
    if (line.size() >= 2 && line[1] == ' ' &&
        (line[0] == '>' || line[0] == '-' || line[0] == '*' || line[0] == '+'))
        return 2;
    return 0;
}

const std::regex &linkPattern()
{
    static const std::regex pattern(R"(\[([^\]\n]+)\]\(([^)\s]+)\))");
    return pattern;
}

void replaceAll(std::string &text, const std::string &from, const std::string &to)
{
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos)
    {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string joined(const std::vector<std::string> &items, const char *separator)
{
    std::string out;
    for (const std::string &item : items)
        out += (out.empty() ? "" : separator) + item;
    return out;
}
} // namespace

std::optional<LengthTier> lengthTierOf(const std::string &length)
{
    if (length == "short")
        return LengthTier::Short;
    if (length == "one")
        return LengthTier::One;
    if (length == "few")
        return LengthTier::Few;
    return std::nullopt;
}

LengthTier lengthTierOr(const std::string &length, LengthTier fallback)
{
    return lengthTierOf(length).value_or(fallback);
}

std::size_t maxSentencesOf(LengthTier tier)
{
    switch (tier)
    {
    case LengthTier::Short:
        return kShortSentences;
    case LengthTier::One:
        return kOneSentences;
    case LengthTier::Few:
    default:
        return kFewSentences;
    }
}

std::size_t maxCodepointsOf(LengthTier tier)
{
    switch (tier)
    {
    case LengthTier::Short:
        return kShortCodepoints;
    case LengthTier::One:
        return kOneCodepoints;
    case LengthTier::Few:
    default:
        return kFewCodepoints;
    }
}

std::size_t codepointCount(const std::string &text)
{
    const std::string trimmed = utils::trim(text);
    std::size_t count = 0;
    for (const unsigned char byte : trimmed)
        if ((byte & 0xC0) != 0x80)
            ++count;
    return count;
}

std::size_t sentenceCount(const std::string &text)
{
    const std::string trimmed = utils::trim(text);
    std::size_t count = 0;
    bool content = false;
    std::size_t pos = 0;
    while (pos < trimmed.size())
    {
        const std::string unit = nextCodepoint(trimmed, pos);
        if (isTerminator(unit))
        {
            if (content)
                ++count;
            content = false;
        }
        else if (!isBlank(unit) && !isPunctuation(unit))
        {
            content = true;
        }
    }
    if (content)
        ++count;
    return count;
}

bool hasMarkdown(const std::string &text)
{
    // “~~”不算：中文聊天里是常见的语气尾巴（“好耶~~”）
    for (const char *marker : {"```", "`", "**", "__"})
        if (text.find(marker) != std::string::npos)
            return true;
    for (const std::string &line : splitLines(text))
        if (blockMarkerLength(leftTrimmed(line)) > 0)
            return true;
    return std::regex_search(text, linkPattern());
}

std::string stripMarkdown(const std::string &text)
{
    std::string out;
    bool first = true;
    for (const std::string &raw : splitLines(text))
    {
        std::string line = leftTrimmed(raw);
        if (line.rfind("```", 0) == 0)
            continue; // 代码围栏整行去掉，围栏里的内容保留
        // 去掉的行首记号可能叠着（"> - 一项"），逐个去
        for (std::size_t marker = blockMarkerLength(line); marker > 0;
             marker = blockMarkerLength(line))
            line = leftTrimmed(line.substr(marker));
        out += (first ? "" : "\n") + line;
        first = false;
    }
    out = std::regex_replace(out, linkPattern(), "$1 $2");
    for (const char *marker : {"```", "**", "__", "`"})
        replaceAll(out, marker, "");
    return utils::trim(out);
}

bool looksLikeSystemText(const std::string &text)
{
    const std::string trimmed = utils::trim(text);
    if (trimmed.rfind("系统提示", 0) == 0)
        return true;
    for (const char *marker : {"[系统注]", "[服务契约]", "[关于你的事实]"})
        if (trimmed.find(marker) != std::string::npos)
            return true;
    return false;
}

ReplyCheck checkReply(const std::string &text, LengthTier tier)
{
    ReplyCheck check;
    check.tier = tier;
    const std::string trimmed = utils::trim(text);
    if (trimmed.empty())
    {
        check.verdict = ReplyCheck::Verdict::Drop;
        check.problems = {"empty"};
        return check;
    }
    if (looksLikeSystemText(trimmed))
    {
        check.verdict = ReplyCheck::Verdict::Drop;
        check.problems = {"system_text"};
        return check;
    }
    check.sentences = sentenceCount(trimmed);
    check.codepoints = codepointCount(trimmed);
    if (check.sentences > maxSentencesOf(tier))
        check.problems.push_back("sentences");
    if (check.codepoints > maxCodepointsOf(tier))
        check.problems.push_back("length");
    if (hasMarkdown(trimmed))
        check.problems.push_back("markdown");
    check.verdict = check.problems.empty() ? ReplyCheck::Verdict::Send : ReplyCheck::Verdict::Retry;
    return check;
}

std::string retryNote(const ReplyCheck &check, const std::string &rejectedText)
{
    std::vector<std::string> parts;
    for (const std::string &problem : check.problems)
    {
        if (problem == "sentences")
            parts.push_back("有 " + std::to_string(check.sentences) + " 句，这一轮最多 " +
                            std::to_string(maxSentencesOf(check.tier)) + " 句");
        else if (problem == "length")
            parts.push_back("有 " + std::to_string(check.codepoints) + " 个字，这一轮最多 " +
                            std::to_string(maxCodepointsOf(check.tier)) + " 个字");
        else if (problem == "markdown")
            parts.push_back("带了 Markdown 记号，群聊里会原样显示");
    }
    const std::string subject = rejectedText.empty()
                                    ? std::string("刚才那句")
                                    : "你刚才要说的「" + utils::trim(rejectedText) + "」";
    return subject + "没有发出：" + joined(parts, "；") + "。照本轮任务重说一次。";
}

SettledReply droppedReply(const ReplyCheck &check)
{
    SettledReply settled;
    settled.deviation = "reply_check:" + joined(check.problems, ",");
    return settled;
}

SettledReply settleReply(const std::string &text, LengthTier tier)
{
    const ReplyCheck check = checkReply(text, tier);
    if (check.verdict == ReplyCheck::Verdict::Send)
        return SettledReply{true, text, {}};
    if (check.verdict == ReplyCheck::Verdict::Drop)
        return droppedReply(check);

    SettledReply settled;
    settled.send = true;
    settled.text = utils::trim(text);
    std::vector<std::string> recorded;
    for (const std::string &problem : check.problems)
    {
        if (problem == "markdown")
        {
            settled.text = stripMarkdown(settled.text);
            recorded.push_back("markdown_stripped");
        }
        else
        {
            recorded.push_back(problem);
        }
    }
    if (settled.text.empty())
        return SettledReply{false, {}, "reply_check:empty"};
    settled.deviation = "reply_check:" + joined(recorded, ",");
    return settled;
}

SettledReply checkWithRetry(const std::string &text, LengthTier tier,
                            const std::function<std::optional<std::string>(const std::string &note)> &retry)
{
    const ReplyCheck first = checkReply(text, tier);
    if (first.verdict == ReplyCheck::Verdict::Send)
        return SettledReply{true, text, {}};
    if (first.verdict == ReplyCheck::Verdict::Drop)
        return droppedReply(first);
    const std::optional<std::string> second = retry ? retry(retryNote(first)) : std::nullopt;
    return settleReply(second ? *second : text, tier);
}

} // namespace affect
