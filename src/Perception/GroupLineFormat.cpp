#include "GroupLineFormat.h"

#include "../utils/LocalDate.h"
#include "../utils/Utils.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <utility>

namespace
{
constexpr std::size_t kQuoteCodepoints = 12;     // 引语长度（码点）
const char *const kImageMarker = "[image asset_id="; // 图片占位符开头（Asset/ImagePlaceholder.h）

// UTF-8 首字节对应的字节数；续字节或非法字节按 1 处理（原样搬运，不制造新的非法序列）
std::size_t utf8Length(unsigned char lead)
{
    if ((lead & 0x80) == 0)
        return 1;
    if ((lead & 0xE0) == 0xC0)
        return 2;
    if ((lead & 0xF0) == 0xE0)
        return 3;
    if ((lead & 0xF8) == 0xF0)
        return 4;
    return 1;
}

bool startsWith(const std::string &text, const std::string &prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

// 图片占位符是否以表情包形式发送（标记在描述 " | " 之前）
bool isStickerPlaceholder(const std::string &placeholder)
{
    const std::string head = placeholder.substr(0, placeholder.find(" | "));
    return head.find(" sticker") != std::string::npos;
}

// 一个非文字标签（图片占位符、群缓冲里的 [视频] [表情 …] 等）的短词；不认识的返回空指针
const char *labelWord(const std::string &label)
{
    if (startsWith(label, kImageMarker))
        return isStickerPlaceholder(label) ? "一张表情包" : "一张图片";
    static const std::pair<const char *, const char *> kWords[] = {
        {"[图片]", "一张图片"},       {"[表情", "一个表情"},      {"[语音]", "一段语音"},
        {"[视频]", "一段视频"},       {"[音乐]", "一首歌"},       {"[文件", "一个文件"},
        {"[卡片", "一张卡片"},        {"[合并转发]", "一条合并转发"},
        {"[非文本消息]", "一条非文字消息"},
    };
    for (const auto &[prefix, word] : kWords)
        if (startsWith(label, prefix))
            return word;
    return nullptr;
}

// 整条只有方括号标签（中间可有空白）且第一个认得：返回它的短词，后面还有别的标签时加“等”；
// 含任何正文或第一个标签不认得时返回空串（按文字引用）
std::string nonTextWord(const std::string &text)
{
    std::string first;
    std::size_t labels = 0;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        const unsigned char byte = static_cast<unsigned char>(text[pos]);
        if (byte == ' ' || byte == '\n' || byte == '\r' || byte == '\t')
        {
            ++pos;
            continue;
        }
        if (byte != '[')
            return {};
        const std::size_t close = text.find(']', pos);
        if (close == std::string::npos)
            return {};
        if (labels == 0)
            first = text.substr(pos, close - pos + 1);
        ++labels;
        pos = close + 1;
    }
    if (labels == 0)
        return {};
    const char *word = labelWord(first);
    if (word == nullptr)
        return {};
    return std::string(word) + (labels > 1 ? "等" : "");
}
} // namespace

std::string replyQuoteOf(const std::string &text)
{
    const std::string trimmed = utils::trim(text);
    const std::string word = nonTextWord(trimmed);
    if (!word.empty())
        return word;

    // 引语正文：图片占位符换成 [图片] / [表情包]（不抄 asset_id），空白折成一个空格；
    // 只取到第 kQuoteCodepoints + 1 个码点为止（多出的那一个用来判断要不要加“…”）
    std::vector<std::string> codepoints;
    const auto append = [&codepoints](const std::string &piece)
    {
        for (std::size_t at = 0; at < piece.size() && codepoints.size() <= kQuoteCodepoints;)
        {
            const std::size_t length =
                std::min(utf8Length(static_cast<unsigned char>(piece[at])), piece.size() - at);
            codepoints.push_back(piece.substr(at, length));
            at += length;
        }
    };
    const std::string marker = kImageMarker;
    std::size_t pos = 0;
    while (pos < trimmed.size() && codepoints.size() <= kQuoteCodepoints)
    {
        if (trimmed.compare(pos, marker.size(), marker) == 0)
        {
            const std::size_t close = trimmed.find(']', pos);
            if (close != std::string::npos)
            {
                append(isStickerPlaceholder(trimmed.substr(pos, close - pos + 1)) ? "[表情包]"
                                                                                 : "[图片]");
                pos = close + 1;
                continue;
            }
        }
        const unsigned char byte = static_cast<unsigned char>(trimmed[pos]);
        if (byte == ' ' || byte == '\n' || byte == '\r' || byte == '\t')
        {
            if (codepoints.empty() || codepoints.back() != " ")
                codepoints.push_back(" ");
            ++pos;
            continue;
        }
        const std::size_t length = std::min(utf8Length(byte), trimmed.size() - pos);
        codepoints.push_back(trimmed.substr(pos, length));
        pos += length;
    }

    const bool longer = codepoints.size() > kQuoteCodepoints;
    if (longer)
        codepoints.resize(kQuoteCodepoints);
    while (longer && !codepoints.empty() && codepoints.back() == " ")
        codepoints.pop_back();
    std::string quote;
    for (const std::string &codepoint : codepoints)
        quote += codepoint;
    return "「" + quote + (longer ? "…" : "") + "」";
}

GroupReplyIndex::GroupReplyIndex(const std::vector<GroupMessageRecord> &records)
{
    for (const GroupMessageRecord &record : records)
    {
        if (record.messageId.empty())
            continue; // 她刚发出、回执还没回填 message_id 的自消息
        targets[record.messageId] = Target{record.isSelf, record.nickname, replyQuoteOf(record.text)};
    }
}

const GroupReplyIndex::Target *GroupReplyIndex::find(const std::string &messageId) const
{
    if (messageId.empty())
        return nullptr;
    const auto found = targets.find(messageId);
    return found != targets.end() ? &found->second : nullptr;
}

std::string groupClock(std::int64_t timestamp)
{
    const std::tm local = localTimeOf(static_cast<std::time_t>(timestamp));
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", local.tm_hour, local.tm_min);
    return buffer;
}

std::string formatGroupLine(const BotIdentity &bot, const GroupMessageRecord &record,
                            const GroupReplyIndex &replies)
{
    std::string line = "[" + groupClock(record.timestamp) + "] " +
                       (record.isSelf ? bot.name + "（你）" : record.nickname);
    if (!record.replyToMessageId.empty())
    {
        if (const GroupReplyIndex::Target *target = replies.find(record.replyToMessageId))
        {
            const std::string who =
                target->isSelf ? std::string("你")
                               : (!target->nickname.empty() ? target->nickname : std::string("群友"));
            line += "（回复 " + who + "：" + target->quote + "）";
        }
        else
        {
            line += "（回复了一条更早的消息）";
        }
    }
    line += ": " + record.text;
    return line;
}
