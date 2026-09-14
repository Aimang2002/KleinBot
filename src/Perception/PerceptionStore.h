#ifndef PERCEPTION_STORE_H
#define PERCEPTION_STORE_H

/*
 * 观察通道的亲密度落库（v2.4.1 T7/T7b）：affinity 表只存计数与
 * speaker_id（加盐单向哈希，见 SpeakerIdentity——T7b 起不存明文 QQ）。
 * 自建连接但共用会话库文件；写只来自 pollingThread 的 flushDue，无并发写。
 */
#include "SpeakerIdentity.h"

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

struct AffinityDelta
{
    std::uint64_t userId = 0; // raw QQ：仅在内存聚合中出现，落库边界转 speaker_id
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

    // 确定性伪名化（与 GroupContextStore 共用同盐；测试与查表用）
    std::string speakerHash(std::uint64_t qq) const
    {
        return perception::speakerIdOf(salt, qq);
    }

    // 批量累加写入（UPSERT）：同 (speaker_id, group_id) 的计数累加而非覆盖
    void upsertBatch(const std::vector<AffinityDelta> &deltas);

private:
    sqlite3 *db = nullptr;
    std::string salt;
};

#endif // PERCEPTION_STORE_H
