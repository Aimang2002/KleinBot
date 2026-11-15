#include <gtest/gtest.h>

#include "Bootstrap/ConfigRules.h"

#include <cstddef>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
// 生效方式只能由两个工厂产生：构造函数私有，applies 传不了空指针
static_assert(!std::is_default_constructible_v<ConfigEffect>, "没有默认的生效方式");
static_assert(!std::is_constructible_v<ConfigEffect, ConfigEffect::Kind, ConfigChangeImpact,
                                       ConfigApplyFunction *>,
              "只能经 restart() / applies() 产生");
static_assert(!std::is_invocable_v<decltype(&applies), std::nullptr_t>,
              "applies 不接受空指针");

void applyNothing(LiveSettings &, const RuntimeSettings &)
{
}
}

// 改造前 90 个；chat.top_p 删除后 89 个
TEST(ConfigRulesTest, ProductTableHasEightyNineKeys)
{
    EXPECT_EQ(productConfigRules().size(), 89U);
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
// L1.a：chat 组除 worker_threads 外的 6 个；L1.b：models 的三个端点各 4 个字段；
// L1.c：voice 的 5 个键（开关与合成参数）；L1.d：memory 除 enabled 外的 4 个；
// L1.e：web_search 除 enabled、provider 外的 8 个，web_fetch 除 enabled 外的 6 个，network.proxy；
// L1.f：群聊开关、affect.enforce、瞥视预算；
// L2.a：web_search.enabled、web_fetch.enabled；L2.b：memory.enabled、glance.enabled；
// L2.c：affect.enabled、affect.policy、affect.judge_model；L2.d：bot.name、bot.aliases
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
        // L2.d 与 L1.f（bot 节在表的最前面）
        "bot.name", "bot.aliases", "bot.group_chat_enabled",
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
        // L2.b 与 L1.d
        "memory.enabled", "memory.model", "memory.batch_turns", "memory.idle_minutes", "memory.recall_limit",
        // L2.c、L1.f 与 L2.b
        "affect.enabled", "affect.policy", "affect.enforce", "affect.judge_model",
        "glance.enabled", "glance.per_group_per_minute",
        // L2.a 与 L1.e
        "web_search.enabled", "web_search.endpoint", "web_search.api_key", "web_search.search_depth",
        "web_search.max_results", "web_search.max_content_chars", "web_search.max_response_bytes",
        "web_search.connect_timeout_ms", "web_search.request_timeout_ms",
        "web_fetch.enabled", "web_fetch.max_content_chars", "web_fetch.max_response_bytes", "web_fetch.connect_timeout_ms",
        "web_fetch.request_timeout_ms", "web_fetch.cache_ttl_seconds", "web_fetch.cache_max_entries",
        "network.proxy",
    };
    EXPECT_EQ(wired.size(), 54U);
    EXPECT_EQ(wired, expected);
}

// L2.e：过渡期的 notYet 已清零删除。每一行要么 restart()（档位 Restart、没有应用函数），
// 要么 applies(函数)（见上一个用例）；只能重启的键按表的顺序逐个列出
TEST(ConfigRulesTest, EveryOtherRowIsRestartOnly)
{
    std::vector<std::string> restartOnly;
    for (const ConfigRule &rule : productConfigRules())
    {
        if (rule.effect.kind() == ConfigEffect::Kind::Applies)
            continue;
        ASSERT_TRUE(rule.effect.kind() == ConfigEffect::Kind::Restart) << rule.path;
        EXPECT_TRUE(rule.effect.impact() == ConfigChangeImpact::Restart) << rule.path;
        EXPECT_TRUE(rule.effect.apply() == nullptr) << rule.path;
        restartOnly.push_back(rule.path);
    }
    const std::vector<std::string> expected = {
        "schema_version", "bot.id", "bot.manager_id", "chat.worker_threads",
        "web_search.provider",
        "storage.conversation_database", "storage.image_assets",
        "communication.protocol.type", "communication.active_transport",
        "communication.active_profile.type", "communication.active_profile.host",
        "communication.active_profile.port", "communication.active_profile.path",
        "communication.active_profile.access_token", "communication.active_profile.api.base_url",
        "communication.active_profile.api.access_token", "communication.active_profile.events.bind",
        "communication.active_profile.events.port", "communication.active_profile.events.path",
        "communication.active_profile.events.access_token",
        "communication.active_profile.events.secret",
        "communication.defaults.connect_timeout_ms", "communication.defaults.request_timeout_ms",
        "communication.defaults.max_event_body_bytes",
        "webui.enabled", "webui.bind", "webui.port", "webui.access_token",
        "push.enabled", "push.bind", "push.port", "push.access_token", "push.allowed_groups",
        "push.allowed_users", "push.max_per_hour",
    };
    EXPECT_EQ(restartOnly.size(), 35U);
    EXPECT_EQ(restartOnly, expected);
}

TEST(ConfigRulesTest, EffectFactoriesTieImpactToApplyFunction)
{
    constexpr ConfigEffect restartOnly = restart();
    EXPECT_TRUE(restartOnly.kind() == ConfigEffect::Kind::Restart);
    EXPECT_TRUE(restartOnly.impact() == ConfigChangeImpact::Restart);
    EXPECT_TRUE(restartOnly.apply() == nullptr);

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
