#ifndef GROUP_LIST_SERVICE_H
#define GROUP_LIST_SERVICE_H

/*
 * 观察通道状态与群列表的唯一所有者（用户定规 2026-09-15：群号相关操作
 * 一律入库，不进配置文件）。
 *
 * 数据库是唯一真值来源：
 *   - perception_meta.feature_enabled  → 观察通道总开关（0/1）
 *   - group_cache.monitored            → 每群是否监控（0/1，1=开启）
 * 配置体系不再有 perception 节；面板改动直接落库并**即时生效**，无需重启。
 *
 * 群列表只是状态服务的附属缓存：启动即从 group_cache 重建镜像（OneBot 未
 * 握手也有数据），就绪后 get_group_list 整表覆写并降频为日级刷新。
 */

#include "../Network/OneBotApiChannel.h"

#include <cstdint>
#include <ctime>
#include <mutex>
#include <set>
#include <string>
#include <vector>

struct sqlite3;

struct GroupListEntry
{
    std::uint64_t groupId = 0;
    std::string name;
    long memberCount = 0;
    std::string avatarUrl; // qlogo 规则 URL，前端 <img> 直用，不落盘
    bool monitored = false;
    std::int64_t refreshedTs = 0;
};

class GroupListService
{
public:
    GroupListService(const std::string &dbPath, OneBotApiChannel &api);

    ~GroupListService();

    GroupListService(const GroupListService &) = delete;
    GroupListService &operator=(const GroupListService &) = delete;

    bool isOpen() const { return db != nullptr; }

    // ---- 观察通道运行状态（DB 为唯一真值；读取即当前生效值）----

    // 总开关：关闭时所有观察入口 no-op（群列表缓存与面板选择器不受影响）
    bool featureEnabled() const;
    bool setFeatureEnabled(bool enabled);

    // 单群监控状态：1=开启。未在群列表中的群号也会落一行（元数据为空），
    // 使手动输入的群号同样可被监控
    bool isMonitored(std::uint64_t groupId) const;
    bool setMonitored(std::uint64_t groupId, bool monitored);

    std::size_t monitoredCount() const;
    std::vector<std::uint64_t> monitoredGroups() const;

    // 面板读路径：镜像快照（monitored 取自库），按人数降序
    std::vector<GroupListEntry> snapshot() const;

    // pollingThread：未就绪时尝试刷新（失败/未连接按冷却静默重试）
    void poll(std::int64_t now);

private:
    void loadStateFromDisk();
    void rebuildMirrorFromDisk();
    void applyFetchedList(const std::vector<GroupListEntry> &fetched, std::int64_t now);
    void persistEnabledUnderLock();
    void persistMonitoredUnderLock();
    bool writeMeta(const std::string &key, const std::string &value);
    std::string readMeta(const std::string &key) const;
    static std::string avatarUrlFor(std::uint64_t groupId);

    sqlite3 *db = nullptr;
    OneBotApiChannel &api;
    mutable std::mutex mutex_;
    std::vector<GroupListEntry> mirror_;
    bool enabled_ = false;              // mutex_ 保护
    std::set<std::uint64_t> monitored_; // mutex_ 保护：监控中的群号集合
    bool ready_ = false;                // get_group_list 首次成功
    std::int64_t nextAttemptTs_ = 0;
};

#endif // GROUP_LIST_SERVICE_H
