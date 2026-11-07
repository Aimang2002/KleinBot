#ifndef BOT_IDENTITY_H
#define BOT_IDENTITY_H

#include <cstdint>
#include <string>

struct BotIdentity
{
    std::uint64_t id = 0;
    std::uint64_t managerId = 0;
    std::string name = "Klein";
    // 她也会答应的另一个名字（bot.aliases，如 "克莱茵"）；空串表示没有
    std::string aliases;
};

namespace bot_identity_detail
{
// 只折叠 ASCII 大小写：逐字节处理 A-Z，UTF-8 多字节序列（最高位为 1）原样保留
inline std::string asciiLower(std::string text)
{
    for (char &c : text)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}
}

// 正文里是否点到她的名字：bot.name 或别名的子串匹配（ASCII 不区分大小写，
// 非 ASCII 精确匹配），空名字忽略
inline bool mentionsBotName(const BotIdentity &bot, const std::string &text)
{
    if (text.empty())
        return false;
    const std::string haystack = bot_identity_detail::asciiLower(text);
    const auto contains = [&haystack](const std::string &name)
    {
        return !name.empty() &&
               haystack.find(bot_identity_detail::asciiLower(name)) != std::string::npos;
    };
    return contains(bot.name) || contains(bot.aliases);
}

#endif
