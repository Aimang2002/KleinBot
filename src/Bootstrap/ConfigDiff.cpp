#include "ConfigDiff.h"
#include "ConfigRules.h"

#include <algorithm>
#include <utility>

bool ConfigDiff::empty() const
{
    return entries.empty();
}

std::size_t ConfigDiff::size() const
{
    return entries.size();
}

std::size_t ConfigDiff::count(ConfigChangeImpact impact) const
{
    return static_cast<std::size_t>(std::count_if(
        entries.begin(), entries.end(), [impact](const ConfigChange &change)
        { return change.impact == impact; }));
}

const std::vector<ConfigChange> &ConfigDiff::changes() const
{
    return entries;
}

void ConfigDiff::add(std::string path, ConfigChangeImpact impact)
{
    entries.push_back({std::move(path), impact});
}

// 档位只来自规则表（ConfigRules.cpp）：逐行比较，按表的顺序记下变化的键
ConfigDiff compareConfig(const SchemaConfig &current, const SchemaConfig &candidate)
{
    ConfigDiff diff;
    for (const ConfigRule &rule : productConfigRules())
    {
        if (rule.changed(current, candidate))
            diff.add(rule.path, rule.effect.impact());
    }
    return diff;
}

std::string configChangeImpactName(ConfigChangeImpact impact)
{
    switch (impact)
    {
    case ConfigChangeImpact::Dynamic:
        return "dynamic";
    case ConfigChangeImpact::Rebuild:
        return "rebuild";
    case ConfigChangeImpact::Restart:
        return "restart";
    }
    return "unknown";
}
