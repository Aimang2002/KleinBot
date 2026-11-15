#include <gtest/gtest.h>

#include "Bootstrap/ConfigRules.h"

#include <cstddef>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
// 生效方式只能由三个工厂产生：构造函数私有，applies 传不了空指针
static_assert(!std::is_default_constructible_v<ConfigEffect>, "没有默认的生效方式");
static_assert(!std::is_constructible_v<ConfigEffect, ConfigEffect::Kind, ConfigChangeImpact,
                                       ConfigApplyFunction *>,
              "只能经 restart() / applies() / notYet() 产生");
static_assert(!std::is_invocable_v<decltype(&applies), std::nullptr_t>,
              "applies 不接受空指针");

void applyNothing(LiveSettings &, const RuntimeSettings &)
{
}
}

TEST(ConfigRulesTest, ProductTableHasNinetyKeys)
{
    EXPECT_EQ(productConfigRules().size(), 90U);
}

TEST(ConfigRulesTest, PathsAreUniqueAndEveryRowIsComplete)
{
    std::set<std::string> paths;
    const SchemaConfig defaults;
    for (const ConfigRule &rule : productConfigRules())
    {
        ASSERT_NE(rule.path, nullptr);
        EXPECT_FALSE(std::string(rule.path).empty());
        EXPECT_TRUE(paths.insert(rule.path).second) << "路径重复：" << rule.path;
        ASSERT_NE(rule.changed, nullptr) << rule.path;
        EXPECT_FALSE(rule.changed(defaults, defaults)) << "同一份配置不应报变化：" << rule.path;
    }
    EXPECT_EQ(paths.size(), productConfigRules().size());
}

// 接通实时生效的键，按表的顺序：每行都带应用函数、档位 Dynamic。
// L1.a：chat 组除 top_p 与 worker_threads 外的 6 个；L1.b：models 的三个端点各 4 个字段；
// L1.c：voice 的 5 个键（开关与合成参数）；L1.d：memory 除 enabled 外的 4 个
TEST(ConfigRulesTest, AppliesRowsAreExactlyTheWiredKeys)
{
    std::vector<std::string> wired;
    for (const ConfigRule &rule : productConfigRules())
    {
        if (rule.effect.kind() != ConfigEffect::Kind::Applies)
            continue;
        wired.push_back(rule.path);
        EXPECT_TRUE(rule.effect.apply() != nullptr) << rule.path;
        EXPECT_TRUE(rule.effect.impact() == ConfigChangeImpact::Dynamic) << rule.path;
    }
    const std::vector<std::string> expected = {
        // L1.a
        "chat.default_model", "chat.temperature", "chat.frequency_penalty", "chat.presence_penalty",
        "chat.max_message_tokens", "chat.message_survival_seconds",
        // L1.b
        "models.drawing.model", "models.drawing.endpoint", "models.drawing.api_key",
        "models.drawing.api_standard", "models.vision.model", "models.vision.endpoint",
        "models.vision.api_key", "models.vision.api_standard", "models.worker.model",
        "models.worker.endpoint", "models.worker.api_key", "models.worker.api_standard",
        // L1.c
        "voice.enabled", "voice.host", "voice.port", "voice.reference_audio_path",
        "voice.reference_text",
        // L1.d
        "memory.model", "memory.batch_turns", "memory.idle_minutes", "memory.recall_limit",
    };
    EXPECT_EQ(wired.size(), 27U);
    EXPECT_EQ(wired, expected);
}

TEST(ConfigRulesTest, EffectFactoriesTieImpactToApplyFunction)
{
    constexpr ConfigEffect restartOnly = restart();
    EXPECT_TRUE(restartOnly.kind() == ConfigEffect::Kind::Restart);
    EXPECT_TRUE(restartOnly.impact() == ConfigChangeImpact::Restart);
    EXPECT_TRUE(restartOnly.apply() == nullptr);

    for (const ConfigChangeImpact impact : {ConfigChangeImpact::Dynamic, ConfigChangeImpact::Rebuild,
                                            ConfigChangeImpact::Restart})
    {
        const ConfigEffect pending = notYet(impact);
        EXPECT_TRUE(pending.kind() == ConfigEffect::Kind::NotYet);
        EXPECT_TRUE(pending.impact() == impact) << "过渡期保留原档位";
        EXPECT_TRUE(pending.apply() == nullptr);
    }

    constexpr ConfigEffect live = applies(applyNothing);
    EXPECT_TRUE(live.kind() == ConfigEffect::Kind::Applies);
    EXPECT_TRUE(live.impact() == ConfigChangeImpact::Dynamic);
    EXPECT_TRUE(live.apply() == &applyNothing);
}

// field<成员指针链> 只看自己那一个字段，嵌套、optional、数组字段都按值比较
TEST(ConfigRulesTest, FieldComparesOnlyItsOwnMember)
{
    const SchemaConfig base;
    ConfigFieldChanged *visionModel =
        field<&SchemaConfig::models, &ModelSchema::vision, &ModelEndpointSchema::model>;
    ConfigFieldChanged *workerThreads = field<&SchemaConfig::chat, &ChatSchema::workerThreads>;
    ConfigFieldChanged *allowedGroups = field<&SchemaConfig::push, &PushSchema::allowedGroups>;
    ConfigFieldChanged *proxy = field<&SchemaConfig::proxy>;

    SchemaConfig candidate = base;
    candidate.models.vision.model = "vision-next";
    EXPECT_TRUE(visionModel(base, candidate));
    EXPECT_FALSE(workerThreads(base, candidate));
    EXPECT_FALSE(allowedGroups(base, candidate));
    EXPECT_FALSE(proxy(base, candidate));

    candidate = base;
    candidate.models.worker.model = "worker-next";
    EXPECT_FALSE(visionModel(base, candidate)) << "同类型的兄弟字段不算";

    candidate = base;
    candidate.chat.workerThreads = 8;
    EXPECT_TRUE(workerThreads(base, candidate));

    candidate = base;
    candidate.push.allowedGroups = {123456};
    EXPECT_TRUE(allowedGroups(base, candidate));

    candidate = base;
    candidate.proxy = "http://127.0.0.1:7890";
    EXPECT_TRUE(proxy(base, candidate));
    EXPECT_FALSE(visionModel(base, candidate));
}
