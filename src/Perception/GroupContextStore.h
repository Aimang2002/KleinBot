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
#include <optional>
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
    // 仅自消息、仅内存：出站投递的 correlation_id，回执到达后据此回填 messageId
    // （R0.2）。不落库——重启后镜像里的自消息要么已有 messageId 要么永远没有
    std::string correlationId;
};

// 冷启动自学习状态（每群一行；EMA/武装期/当日帽/冷却截止的持久化形态）
struct EngagementColdRow
{
    double hourlyEma = -1.0;        // 每小时消息数 EMA；-1 = 未学习
    std::int64_t armedTs = 0;       // 武装期起算（首条消息）
    std::int64_t dayAnchor = 0;     // 当日帽的日界锚点
    int coldToday = 0;              // 今日冷启动评估次数
    std::int64_t cooldownUntil = 0; // 冷启动禁入截止
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

    // 自消息 message_id 回填（R0.2）：按 correlationId 找镜像里的自消息，补上协议端
    // 返回的真实 message_id（镜像 + 落库行）。返回 false = 找不到（已淘汰/从未登记）
    bool backfillMessageId(const std::string &correlationId, const std::string &messageId);

    // 改写一条记录的文本（R1.1 瞥视写回图片描述）：镜像 + 落库行。
    // 返回 false = 该群镜像里已无此 seq（已淘汰），不落库
    bool updateText(std::uint64_t groupId, std::int64_t seq, const std::string &text);
    // 读一条记录的当前文本（锁内拷贝）；已淘汰返回 nullopt
    std::optional<std::string> findText(std::uint64_t groupId, std::int64_t seq) const;

    // TTL/条数清理：pollingThread 周期调用；now 为 unix 秒
    void prune(std::int64_t now);

    // 某群当前镜像（时间序）；groupContext/选择器读路径，锁内快照拷贝
    std::vector<GroupMessageRecord> snapshot(std::uint64_t groupId) const;

    // 冷启动自学习状态：启动时整表读入，状态变更时 UPSERT（低频）
    std::map<std::uint64_t, EngagementColdRow> loadEngagementCold() const;
    void saveEngagementCold(std::uint64_t groupId, const EngagementColdRow &row);

private:
    sqlite3 *db = nullptr;
    mutable std::mutex mutex;
    std::map<std::uint64_t, std::deque<GroupMessageRecord>> mirrors;
    std::int64_t nextSeq = 1;
};

#endif // GROUP_CONTEXT_STORE_H
