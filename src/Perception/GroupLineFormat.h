#ifndef GROUP_LINE_FORMAT_H
#define GROUP_LINE_FORMAT_H

/*
 * 群聊材料一行的唯一格式化点（经历层 E0，ADR-026）。会话轮材料（原文窗口、压缩块、尾巴）、
 * 上下文召回结果、@ 回复的群背景（直灌块与摘要输入）都经这里，行格式只有一份：
 *     [15:49] 阿青: 我们部门被边缘化了
 *     [15:50] Klein（你）（回复 阿青：「我们部门被边缘化了」）: 闲下来也挺好，摸鱼不用愧疚
 *     [15:51] 阿青（回复 你：「闲下来也挺好，摸鱼不用愧…」）: 也是
 *     [15:52] 老余（回复 阿青：一张图片）: 哈哈哈
 *     [15:53] 小白（回复了一条更早的消息）: 对
 * - 被回复的是她自己写“你”，是别人写昵称；
 * - 发言人是部署者（QQ 号等于 bot.manager_id）时昵称后加“（部署者）”（ADR-032，2026-11-22）；
 * - 引语取被回复消息的前 12 个码点（UTF-8 安全，空白折成一个空格），更长加“…”；
 * - 被回复的只是图片、表情等非文字内容时写短词（“一张图片”），不抄 asset_id；文字里夹着的
 *   图片占位符在引语里换成“[图片]”；
 * - 被回复的消息不在索引里（已过期或是更早的消息）时写“（回复了一条更早的消息）”；
 * - 没有引用时与此前的格式逐字相同。
 * 这是感知层的忠实度，不受 affect.enabled 控制。
 */
#include "GroupContextStore.h"
#include "../Application/BotIdentity.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// 按 messageId 查被回复消息的索引：由调用方手里的全部记录构建（不只是入选的几条），
// 只留下画回复关系要用的几项（拷贝，不引用调用方的记录）
class GroupReplyIndex
{
public:
    struct Target
    {
        bool isSelf = false;  // 被回复的是她自己的发言
        std::string nickname; // 被回复者的昵称快照
        std::string quote;    // 「前 12 码点…」，或非文字内容的短词（一张图片）
    };

    GroupReplyIndex() = default;
    explicit GroupReplyIndex(const std::vector<GroupMessageRecord> &records);

    // 找不到（id 为空、已过期、更早的消息）返回空指针
    const Target *find(const std::string &messageId) const;

private:
    std::unordered_map<std::string, Target> targets;
};

// 一行里的时刻：运行机器本地时间 HH:MM
std::string groupClock(std::int64_t timestamp);

// 被回复消息在行里的样子：「前 12 码点…」或短词（GroupReplyIndex 建索引时用，单测可直接调）
std::string replyQuoteOf(const std::string &text);

// 一行群聊材料（不含行尾换行）：她自己的发言人写 bot.name + "（你）"
std::string formatGroupLine(const BotIdentity &bot, const GroupMessageRecord &record,
                            const GroupReplyIndex &replies);

#endif // GROUP_LINE_FORMAT_H
