#ifndef CONFIG_RULES_H
#define CONFIG_RULES_H

/*
 * 配置规则表（ADR-023，配置实时生效）：每个配置键一行——差异里显示的路径、
 * 比较哪个 Schema 字段、怎么生效。比较（compareConfig）、应用与反馈（ConfigReloader）、
 * 测试都遍历同一张表，档位只来自这里。新增配置键 = 表里加一行，并回答“怎么生效”。
 *
 * 生效方式只能由三个工厂产生（ConfigEffect 的构造函数是私有的）：
 *   restart()       只能重启生效。档位 Restart，没有应用函数。
 *   applies(函数)   重载成功后调用该函数，把新值换进活值。档位 Dynamic。
 *                   参数是函数引用，传不了空指针：没有应用函数就造不出“实时生效”。
 *   notYet(原档位)  过渡期：这个键还没接通实时生效，保留改造前的档位，没有应用函数；
 *                   重载反馈里如实列为“尚未接通实时生效、需重启”。L2 结束时清零并删除。
 *
 * 只有 Bootstrap、组合根、面板与测试包含本头文件（业务模块不得包含 Bootstrap/，
 * 由 tests/architecture_dependency_test.py 机检）。
 */
#include "ConfigDiff.h"
#include "../Configuration/SchemaConfig.h"

#include <cstddef>

struct LiveSettings;
struct RuntimeSettings;

// 应用函数：把新的运行时设置换进对应的活值。只替换快照，不会失败
using ConfigApplyFunction = void(LiveSettings &live, const RuntimeSettings &next);

class ConfigEffect
{
public:
    enum class Kind
    {
        Restart, // restart()
        Applies, // applies(函数)
        NotYet   // notYet(原档位)，过渡期
    };

    constexpr Kind kind() const { return kindValue; }
    constexpr ConfigChangeImpact impact() const { return impactValue; }
    // 只有 applies 产生的生效方式非空
    constexpr ConfigApplyFunction *apply() const { return applyFunction; }

private:
    constexpr ConfigEffect(Kind kind, ConfigChangeImpact impact, ConfigApplyFunction *apply)
        : kindValue(kind), impactValue(impact), applyFunction(apply)
    {
    }

    Kind kindValue;
    ConfigChangeImpact impactValue;
    ConfigApplyFunction *applyFunction;

    friend constexpr ConfigEffect restart();
    friend constexpr ConfigEffect applies(ConfigApplyFunction &apply);
    friend constexpr ConfigEffect notYet(ConfigChangeImpact current);
};

constexpr ConfigEffect restart()
{
    return ConfigEffect(ConfigEffect::Kind::Restart, ConfigChangeImpact::Restart, nullptr);
}

constexpr ConfigEffect applies(ConfigApplyFunction &apply)
{
    return ConfigEffect(ConfigEffect::Kind::Applies, ConfigChangeImpact::Dynamic, &apply);
}

constexpr ConfigEffect notYet(ConfigChangeImpact current)
{
    return ConfigEffect(ConfigEffect::Kind::NotYet, current, nullptr);
}

// 比较函数：同一个字段在两份配置里是否不同
using ConfigFieldChanged = bool(const SchemaConfig &current, const SchemaConfig &candidate);

struct ConfigRule
{
    const char *path;            // 差异条目与反馈里的路径（与改造前逐字相同）
    ConfigEffect effect;         // restart() / applies(函数) / notYet(原档位)
    ConfigFieldChanged *changed; // field<成员指针链>
};

namespace config_rules_detail
{
// 沿成员指针链取字段：fieldOf<&SchemaConfig::chat, &ChatSchema::temperature>(config)
// 就是 config.chat.temperature
template <auto Member, auto... Rest, typename Object>
const auto &fieldOf(const Object &object)
{
    if constexpr (sizeof...(Rest) == 0)
        return object.*Member;
    else
        return fieldOf<Rest...>(object.*Member);
}
}

// 规则行的比较函数。模板实参是从 SchemaConfig 出发的成员指针链，嵌套字段同样适用：
//   field<&SchemaConfig::chat, &ChatSchema::temperature>
//   field<&SchemaConfig::models, &ModelSchema::vision, &ModelEndpointSchema::model>
// 链上任何一段的名字或所属结构写错都编译不过；每组实参实例化成一个普通函数，
// 直接放进表里就是函数指针
template <auto... Path>
bool field(const SchemaConfig &current, const SchemaConfig &candidate)
{
    return config_rules_detail::fieldOf<Path...>(current) !=
           config_rules_detail::fieldOf<Path...>(candidate);
}

// 一张规则表：常量数组的首地址与行数（C++17 没有 std::span），可直接用于范围 for
struct ConfigRuleTable
{
    const ConfigRule *rows = nullptr;
    std::size_t count = 0;

    const ConfigRule *begin() const { return rows; }
    const ConfigRule *end() const { return rows + count; }
    std::size_t size() const { return count; }
};

template <std::size_t N>
constexpr ConfigRuleTable ruleTable(const ConfigRule (&rows)[N])
{
    return ConfigRuleTable{rows, N};
}

// 产品规则表：90 个配置键，表的顺序就是差异条目的输出顺序
ConfigRuleTable productConfigRules();

#endif // CONFIG_RULES_H
