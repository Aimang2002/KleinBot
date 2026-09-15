#ifndef GROUP_LIST_SERVICE_H
#define GROUP_LIST_SERVICE_H

/*
 * 群列表缓存服务（T7c）：get_group_list 的本地缓存 + 内存镜像，
 * 供 WebUI 白名单选择器展示群名称/人数/头像/是否监控。
 *
 * 生命周期：启动即从 group_cache 表建镜像（OneBot 未握手也有数据）；
 * OneBot 就绪后（组合根在 CapabilityBroker 探测完成后调 refresh）经
 * get_group_list 拉取，成功整表覆写并同步镜像，失败保留旧缓存按
 * 冷却重试（同能力探测的 polling 模式）。纯缓存非用户数据——换机器人
 * 后旧群自然被覆写，消失的群在刷新时清除。
 *
 * monitored 标记是白名单选择器的持久勾选状态（用户定规：入库字段）：
 * 前端勾选/取消只改这一列，perception.observe_groups 的配置真值仍由
 * 面板保存流程写 .config.json——两者经 GET /api/groups 对齐展示。
 */
#include "PerceptionOptions.h"
#include "../Network/OneBotApiChannel.h"

#include <algorithm>
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
    GroupListService(const std::string &dbPath, OneBotApiChannel &api,
                     const PerceptionOptions &options);

    ~GroupListService();

    GroupListService(const GroupListService &) = delete;
    GroupListService &operator=(const GroupListService &) = delete;

    bool isOpen() const { return db != nullptr; }

    // 面板读路径：内存镜像快照（monitored 与当前配置白名单对齐标注）
    std::vector<GroupListEntry> snapshot() const;

    // pollingThread：未就绪时尝试刷新（失败/未连接按冷却静默重试）
    void poll(std::int64_t now);

    // 白名单落库（用户定规：monitored 0/1，1=开启监控）：
    // 把观察白名单同步进 group_cache.monitored——配置仍是唯一真值来源
    // （.config.json），本列是它的落盘副本，供直接查库与审计。
    // 启动时与面板保存配置后各调一次；不在机器人群列表里的白名单群号
    // 无行可写（只影响落盘副本，不影响实际观察）。
    void applyWhitelist(const std::vector<std::uint64_t> &observeGroups);

    // 配置变化入口（组合根/面板调用）：等价于 applyWhitelist + 记住新选项
    void onOptionsChanged(const PerceptionOptions &options);

private:
    void rebuildMirrorFromDisk();
    void applyFetchedList(const std::vector<GroupListEntry> &fetched, std::int64_t now);
    void persistMonitoredUnderLock();
    static std::string avatarUrlFor(std::uint64_t groupId);

    sqlite3 *db = nullptr;
    OneBotApiChannel &api;
    PerceptionOptions options; // mutex_ 下读写
    mutable std::mutex mutex_;
    std::vector<GroupListEntry> mirror_;
    bool ready_ = false;      // get_group_list 首次成功
    std::int64_t nextAttemptTs_ = 0;
};

#endif // GROUP_LIST_SERVICE_H
