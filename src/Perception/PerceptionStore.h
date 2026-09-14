#ifndef PERCEPTION_STORE_H
#define PERCEPTION_STORE_H

/*
 * 观察通道的亲密度落库（v2.4.1 T7）：affinity 表只存计数，
 * 不存任何消息内容（D7 红线：原文永不落盘）。自建连接但共用
 * 会话库文件；写只来自 pollingThread 的 flushDue，无并发写。
 */
#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

struct AffinityDelta
{
    std::uint64_t userId = 0;
    std::uint64_t groupId = 0;
    long observedDelta = 0;
    long interactionDelta = 0;
    std::int64_t lastSeenTs = 0;
};

class PerceptionStore
{
public:
    explicit PerceptionStore(const std::string &dbPath);
    ~PerceptionStore();

    PerceptionStore(const PerceptionStore &) = delete;
    PerceptionStore &operator=(const PerceptionStore &) = delete;

    bool isOpen() const { return db != nullptr; }

    // 批量累加写入（UPSERT）：同 (user_id, group_id) 的计数累加而非覆盖
    void upsertBatch(const std::vector<AffinityDelta> &deltas);

private:
    sqlite3 *db = nullptr;
};

#endif // PERCEPTION_STORE_H
