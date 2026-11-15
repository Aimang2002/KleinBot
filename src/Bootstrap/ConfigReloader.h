#ifndef CONFIG_RELOADER_H
#define CONFIG_RELOADER_H

/*
 * 唯一的“重载并应用”入口（ADR-023）：#刷新配置文件 与面板保存都调 reload()。
 *
 * 一次 reload()：
 *   1. 串行化（并发的重载排队，发布与应用不会交错）；
 *   2. 取旧快照，调 ConfigSnapshotStore::reload() 读文件并完整校验——
 *      校验不过则快照与活值都不变；
 *   3. 校验通过、新快照发布之后，按规则表逐行比较新旧配置，把变化的键归为
 *      已应用 / 需重启两类；带应用函数的行执行应用，同一个应用函数
 *      一次重载至多调用一次。应用只替换活值快照，不会失败。
 *
 * 面板只拿 ConfigSnapshotStore 的 const 引用（取快照），重载只能经这里。
 */
#include "ConfigRules.h"
#include "ConfigSnapshotStore.h"

#include <mutex>
#include <string>
#include <vector>

struct ConfigReloadReport
{
    ConfigReloadResult result;             // 快照发布结果，与 ConfigSnapshotStore::reload() 相同
    std::vector<std::string> applied;      // applies 行：新值已换进活值
    std::vector<std::string> restartOnly;  // restart() 行：只能重启生效
};

class ConfigReloader
{
public:
    // rules 默认是产品规则表；测试可传入自己的小表
    ConfigReloader(ConfigSnapshotStore &store, LiveSettings &live,
                   ConfigRuleTable rules = productConfigRules());

    ConfigReloadReport reload();

private:
    ConfigSnapshotStore &store;
    LiveSettings &live;
    const ConfigRuleTable rules;
    std::mutex reloadMutex;
};

// 重载后的反馈文案：已生效几项；需重启的列出路径
std::string summarizeReload(const ConfigReloadReport &report);

#endif // CONFIG_RELOADER_H
