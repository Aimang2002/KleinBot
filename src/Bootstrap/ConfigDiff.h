#ifndef CONFIG_DIFF_H
#define CONFIG_DIFF_H

#include "../Configuration/SchemaConfig.h"

#include <cstddef>
#include <string>
#include <vector>

enum class ConfigChangeImpact
{
    Dynamic,
    Rebuild, // 配置实时生效 L2.e 起不再产生（规则表只有 restart()/applies()），枚举值与 JSON 名保留
    Restart
};

struct ConfigChange
{
    std::string path;
    ConfigChangeImpact impact;
};

class ConfigDiff
{
public:
    bool empty() const;
    std::size_t size() const;
    std::size_t count(ConfigChangeImpact impact) const;
    const std::vector<ConfigChange> &changes() const;

    void add(std::string path, ConfigChangeImpact impact);

private:
    std::vector<ConfigChange> entries;
};

ConfigDiff compareConfig(const SchemaConfig &current, const SchemaConfig &candidate);
std::string configChangeImpactName(ConfigChangeImpact impact);

#endif
