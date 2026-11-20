#include "FactsCard.h"

#include "../Log/Log.h"
#include "../utils/LocalDate.h"
#include "../utils/Utils.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace affect
{
namespace
{
const char *const kWeekdays[] = {"星期日", "星期一", "星期二", "星期三",
                                 "星期四", "星期五", "星期六"};

// 不超过 limit 字节的最长 UTF-8 完整前缀的长度：limit 处若是续字节，退回到那个字符的首字节
//（合法 UTF-8 至多退 3 字节；不合法的输入也只退 3 字节，不再往前找）
std::size_t utf8PrefixLength(const std::string &text, std::size_t limit)
{
    if (text.size() <= limit)
        return text.size();
    std::size_t cut = limit;
    for (int step = 0; step < 3 && cut > 0; ++step)
    {
        if ((static_cast<unsigned char>(text[cut]) & 0xC0) != 0x80)
            break;
        --cut;
    }
    return cut;
}
} // namespace

std::string sceneFactsNote(std::time_t now)
{
    const std::tm local = localTimeOf(now);
    char clock[8];
    std::snprintf(clock, sizeof(clock), "%02d:%02d", local.tm_hour, local.tm_min);
    const int weekday = local.tm_wday >= 0 && local.tm_wday <= 6 ? local.tm_wday : 0;
    return "现在是 " + std::to_string(local.tm_year + 1900) + "年" +
           std::to_string(local.tm_mon + 1) + "月" + std::to_string(local.tm_mday) + "日 " +
           kWeekdays[weekday] + " " + clock + "。这一轮你能用的工具，就是提供给你的这些。";
}

std::string deployerFactsSection(const std::string &raw)
{
    const std::string facts = utils::trim(raw);
    if (facts.empty())
        return {};
    return "\n\n[关于你的事实] 由部署者填写，是真实情况：\n" + facts;
}

DeployerFacts::DeployerFacts(std::string path) : filePath(std::move(path)) {}

std::string DeployerFacts::read() const
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open())
        return {};
    // 多读 1 字节：据此判断是否超长，并看清截断处是不是落在一个字符中间
    std::string content(kMaxBytes + 1, '\0');
    input.read(&content[0], static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<std::size_t>(input.gcount()));
    if (content.size() <= kMaxBytes)
    {
        warnedSize.store(0);
        return content;
    }

    content.resize(utf8PrefixLength(content, kMaxBytes));
    std::error_code error;
    const std::uintmax_t total = std::filesystem::file_size(filePath, error);
    // 同一大小只告警一次：每个群聊回合都会读它，超长不改就不必每轮刷日志
    const std::uintmax_t key = error ? static_cast<std::uintmax_t>(kMaxBytes + 1) : total;
    if (warnedSize.exchange(key) != key)
        LOG_WARNING("部署者事实文件超过 " + std::to_string(kMaxBytes) + " 字节" +
                    (error ? std::string() : "（" + std::to_string(total) + " 字节）") +
                    "，只取前 " + std::to_string(content.size()) + " 字节：" + filePath);
    return content;
}

FactsCard factsCardFor(bool affectEnabled, const DeployerFacts *facts, std::time_t now)
{
    FactsCard card;
    if (!affectEnabled || facts == nullptr)
        return card;
    card.systemSection = deployerFactsSection(facts->read());
    card.sceneNote = sceneFactsNote(now);
    return card;
}
} // namespace affect
