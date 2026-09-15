#ifndef GROUP_CONTEXT_STORE_H
#define GROUP_CONTEXT_STORE_H

/*
 * 群聊内容库（T7b，D7 修订版）：监控群的短时消息原文缓冲。
 * 上限：每群最近 300 条 + 24 小时 TTL（用户定规）；user_id 存原始 QQ
 * （用户定规 2026-09-15：不做伪名化），昵称随消息快照（摘要可读性）；
 * 永不进入长期记忆与日志。读路径走内存镜像（每群 deque），SQLite
 * 仅 write-through 落盘供重启恢复；写发生在 worker 观察路径。
 */

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

// 一条群消息记录（内存镜像与落库行同构）
struct GroupMessageRecord
{
    std::int64_t seq = 0;            // 全局自增，群内时间序
    std::uint64_t groupId = 0;
    std::uint64_t userId = 0;        // 原始 QQ（Klein 自己的出站为 bot.id）
    std::string nickname;            // 发言人昵称/名片快照（或 "Klein"）
    std::string text;                // 原文或出站占位符（[图片]/[语音]…）
    std::int64_t timestamp = 0;
    bool mentionsBot = false;        // @bot 或正文含 bot 名字（信箱标记）
    bool isSelf = false;             // Klein 自己的出站
    std::string messageId;           // 原始 message_id（数字或字符串形态），回复链解析用
    std::string replyToMessageId;    // 被引用消息的原始 id，空为无引用
};

class GroupContextStore
{
public:
    explicit GroupContextStore(const std::string &dbPath);
    ~GroupContextStore();

    GroupContextStore(const GroupContextStore &) = delete;
    GroupContextStore &operator=(const GroupContextStore &) = delete;

    bool isOpen() const { return db != nullptr; }

    // 追加一条消息（入站或 Klein 出站），返回 seq；群超上限时淘汰最旧并同步删库
    std::int64_t append(const GroupMessageRecord &record);

    // TTL/条数清理：pollingThread 周期调用；now 为 unix 秒
    void prune(std::int64_t now);

    // 某群当前镜像（时间序）；groupContext/选择器读路径，锁内快照拷贝
    std::vector<GroupMessageRecord> snapshot(std::uint64_t groupId) const;

private:
    sqlite3 *db = nullptr;
    mutable std::mutex mutex;
    std::map<std::uint64_t, std::deque<GroupMessageRecord>> mirrors;
    std::int64_t nextSeq = 1;
};

#endif // GROUP_CONTEXT_STORE_H
