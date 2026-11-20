#ifndef AFFECT_FACTS_CARD_H
#define AFFECT_FACTS_CARD_H

/*
 * 事实卡（经历层 E0，ADR-026）：她每轮手里关于“此刻”和“她自己”的真实情况。
 *   - 现场事实：日期、星期、时刻，以及“这一轮能用的工具就是提供给你的这些”，由程序算出。
 *     每分钟都变，只进本轮 user 侧（请求副本，不落库），绝不进 system——system 在请求前缀
 *     的最前端，逐字变化会让供应商的前缀缓存整段失效。
 *   - 部署者事实：部署者写的 source/facts.md（与 soul.md 同目录，可选），稳定文本，进 system，
 *     跟在人格与服务契约之后；文件缺失或去掉首尾空白后为空时整段不加。
 * 这里只有格式与机制，关于她的事实内容全部来自部署者的文件，不写进可执行文件
 *（用户 2026-11-16 定规）。affect.enabled 为假或调用方没接线时什么都不加，请求与此前逐字相同。
 */
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>

namespace affect
{
// 现场事实一句话（运行机器的本地时间），不带方括号标签，放哪、加什么标签由调用方决定：
// "现在是 2026年11月16日 星期一 15:51。这一轮你能用的工具，就是提供给你的这些。"
std::string sceneFactsNote(std::time_t now);

// 部署者事实段（进 system）：去首尾空白后为空返回空串，否则
// "\n\n[关于你的事实] 由部署者填写，是真实情况：\n" + 去首尾空白后的正文
std::string deployerFactsSection(const std::string &raw);

// 部署者写的事实文件（可选）。read() 每次现读：文件很小，改了下一次请求即生效（与 soul 现读
// 同一思路）；文件缺失或读不了返回空串，不报错。资源护栏：超过 kMaxBytes 字节只取前面，按 UTF-8
// 边界截断，并记一条 WARNING（只记长度，不记内容；同一大小只记一次）。多线程同时读安全
class DeployerFacts
{
public:
    static constexpr std::size_t kMaxBytes = 4096;

    // maxBytes：读取上限（资源护栏）；facts.md 用默认 4096，判定器读 soul.md 时组合根给更大的值
    explicit DeployerFacts(std::string path, std::size_t maxBytes = kMaxBytes);
    DeployerFacts(const DeployerFacts &) = delete;
    DeployerFacts &operator=(const DeployerFacts &) = delete;

    std::string read() const;
    const std::string &path() const { return filePath; }

private:
    std::string filePath;
    std::size_t maxBytes;
    mutable std::atomic<std::uintmax_t> warnedSize{0}; // 已为之告警的超长文件大小（0 = 没有）
};

// 一轮的事实卡：两条群聊路径共用的装配点，调用方只决定放哪
struct FactsCard
{
    std::string systemSection; // deployerFactsSection(文件原文)：进 system，跟在人格与契约之后
    std::string sceneNote;     // sceneFactsNote(now)：进本轮 user 侧
};

// affectEnabled 为假或 facts 为空（未接线）时两项都为空、不读文件；否则现场事实总有，
// 部署者事实段按文件内容决定（为空则不加）
FactsCard factsCardFor(bool affectEnabled, const DeployerFacts *facts, std::time_t now);
} // namespace affect

#endif // AFFECT_FACTS_CARD_H
