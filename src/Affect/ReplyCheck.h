#ifndef AFFECT_REPLY_CHECK_H
#define AFFECT_REPLY_CHECK_H

/*
 * 结构核对（经历层 E1“后核”，ADR-027）：计划 v2 生效时，她要说出口的那句话在发出前经这里。
 * 纯函数，只看结构，不看措辞，没有词语黑名单：
 *   - 空文本                         → 不发（Drop）
 *   - 程序的错误串或系统提示的标记   → 不发（Drop）。根源是错误串与她的话分开传递，这里只是最后一道
 *   - 句数超过本档（short/one 1 句，few 3 句）           → 重说（Retry）
 *   - 码点数超过本档护栏（short 24，one 60，few 150）     → 重说（Retry）
 *   - 带 Markdown 记号                                   → 重说（Retry）
 * 重说只有一次，告诉她具体哪里不合格（retryNote）。第二次仍是句数或字数问题时照样发出
 *（被叫到时不能因为话长了就不回），仍带 Markdown 时去掉记号后发出；两种都记一笔偏差，
 * 写进 affect_executions.deviation_reason（"reply_check:…"）。
 * 档位上限是资源护栏类的代码常量（宽松，只防失控），不是配置，也不是她的说话偏好。
 */
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace affect
{
enum class LengthTier
{
    Short, // 一短句
    One,   // 一句
    Few,   // 两三句
};

// "short" / "one" / "few"；其余（含空串）返回空
std::optional<LengthTier> lengthTierOf(const std::string &length);
// 计划没给长度档时（离场、不说）核对用最宽的一档
LengthTier lengthTierOr(const std::string &length, LengthTier fallback = LengthTier::Few);

std::size_t maxSentencesOf(LengthTier tier);  // 1 / 1 / 3
std::size_t maxCodepointsOf(LengthTier tier); // 24 / 60 / 150

// 结构计量（去首尾空白后计）
std::size_t codepointCount(const std::string &text);
// 句数：以 。！？!? 与换行断句，连着的句末标点与紧随的收尾引号、括号算同一处；
// 一段里有一个非空白、非标点的字符才算一句（"哈哈哈！！！" 是 1 句）
std::size_t sentenceCount(const std::string &text);
// Markdown 记号：``` ` ** __、行首的 # 标题、> 引用、- * + 列表、[文字](链接)。
// “~~”不算（中文聊天里常见的语气尾巴“好耶~~”），也不去掉
bool hasMarkdown(const std::string &text);
// 去掉上面这些记号（代码围栏整行去掉，链接写成“文字 链接”），其余逐字保留
std::string stripMarkdown(const std::string &text);
// 程序的错误串（"系统提示…" 开头）或系统提示里的段落标记（[系统注] [服务契约] [关于你的事实]）
bool looksLikeSystemText(const std::string &text);

struct ReplyCheck
{
    enum class Verdict
    {
        Send,
        Drop,
        Retry,
    };
    Verdict verdict = Verdict::Send;
    // 不合格的项：empty / system_text（Drop）；sentences / length / markdown（Retry）
    std::vector<std::string> problems;
    std::size_t sentences = 0;
    std::size_t codepoints = 0;
    LengthTier tier = LengthTier::Few;
};

ReplyCheck checkReply(const std::string &text, LengthTier tier);

// 重说的提示（中文，告诉她哪里不合格）。rejectedText 非空时把她刚才那句写进去
//（会话轮的重说没有 assistant 消息可看）；@ 回复的重说前面已有她那句 assistant 消息，传空串
std::string retryNote(const ReplyCheck &check, const std::string &rejectedText = {});

// 一句话最后怎么处理
struct SettledReply
{
    bool send = false;
    std::string text;      // 要发出的文字（去掉 Markdown 记号后）
    std::string deviation; // 空 = 无偏差；否则 "reply_check:<项,项>"
};

// 第二次（或无法重说时的第一次）的结论：Drop 的不发；句数、字数照样发出，Markdown 去掉记号
// 后发出，都记偏差（markdown 记作 markdown_stripped）
SettledReply settleReply(const std::string &text, LengthTier tier);

// 第一次就不发（Drop）时的结论与偏差
SettledReply droppedReply(const ReplyCheck &check);

// 核对 → 至多重说一次 → 结论。retry 收到 retryNote(检查结果) 返回第二次的文字；重说失败
//（模型没回、只要工具）返回空，此时按第一次的文字下结论
SettledReply checkWithRetry(const std::string &text, LengthTier tier,
                            const std::function<std::optional<std::string>(const std::string &note)> &retry);

} // namespace affect

#endif // AFFECT_REPLY_CHECK_H
