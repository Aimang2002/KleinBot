#include "ConfigReloader.h"

#include <algorithm>
#include <memory>

namespace
{
std::string joinPaths(const std::vector<std::string> &paths)
{
    std::string text;
    for (const std::string &path : paths)
    {
        if (!text.empty())
            text += "、";
        text += path;
    }
    return text;
}
}

ConfigReloader::ConfigReloader(ConfigSnapshotStore &store, LiveSettings &live,
                               ConfigRuleTable rules)
    : store(store), live(live), rules(rules)
{
}

ConfigReloadReport ConfigReloader::reload()
{
    std::lock_guard<std::mutex> lock(reloadMutex);
    const std::shared_ptr<const ConfigSnapshot> before = store.current();

    ConfigReloadReport report;
    report.result = store.reload();
    if (!report.result.success)
        return report;

    const SchemaConfig &previous = *before->schema;
    const SchemaConfig &next = *report.result.snapshot->schema;
    std::vector<ConfigApplyFunction *> pending;
    for (const ConfigRule &rule : rules)
    {
        if (!rule.changed(previous, next))
            continue;
        switch (rule.effect.kind())
        {
        case ConfigEffect::Kind::Applies:
            report.applied.push_back(rule.path);
            if (std::find(pending.begin(), pending.end(), rule.effect.apply()) == pending.end())
                pending.push_back(rule.effect.apply());
            break;
        case ConfigEffect::Kind::Restart:
            report.restartOnly.push_back(rule.path);
            break;
        case ConfigEffect::Kind::NotYet:
            report.notWired.push_back(rule.path);
            break;
        }
    }

    // 应用函数换的是一整组活值：同组几个键一起变化时只换一次
    for (ConfigApplyFunction *apply : pending)
        apply(live, report.result.snapshot->runtime);
    return report;
}

std::string summarizeReload(const ConfigReloadReport &report)
{
    if (!report.result.success)
        return "配置刷新失败，继续使用当前内存快照。";

    const std::size_t total =
        report.applied.size() + report.restartOnly.size() + report.notWired.size();
    if (total == 0)
        return "配置校验通过，内存快照无变化。";

    std::string text = "配置已刷新：" + std::to_string(total) + " 项变化，已生效 " +
                       std::to_string(report.applied.size()) + " 项。";
    if (!report.restartOnly.empty())
        text += "\n需重启才能生效 " + std::to_string(report.restartOnly.size()) + " 项：" +
                joinPaths(report.restartOnly) + "。";
    if (!report.notWired.empty())
        text += "\n尚未接通实时生效、需重启 " + std::to_string(report.notWired.size()) + " 项：" +
                joinPaths(report.notWired) + "。";
    return text;
}
