#include <gtest/gtest.h>

#include "Action/WebSearchAction.h"
#include "Tool/ActionTool.h"
#include "Port/ToolNames.h"
#include "Bootstrap/RuntimeSettings.h"
#include "Configuration/ConfigLoader.h"
#include "WebSearch/SearchProvider.h"
#include "WebSearch/TavilySearchProvider.h"
#include "utils/Live.h"
#include <cstdlib>

namespace
{
class FakeSearchProvider : public SearchProvider
{
public:
    SearchResponse search(const SearchRequest &request) override
    {
        lastRequest = request;
        SearchResponse response;
        response.results = results;
        response.answer = answer;
        return response;
    }

    SearchRequest lastRequest;
    std::vector<SearchResult> results;
    std::string answer;
};
}

TEST(WebSearchActionTest, PassesModelTimePolicyToProvider)
{
    FakeSearchProvider provider;
    WebSearchOptions options;
    options.maxResults = 3;
    Live<WebSearchOptions> liveOptions(options);
    WebSearchAction action(provider, liveOptions);

    const ActionResult result = action.execute(
        {{"query", "中东局势"}, {"topic", "news"}, {"days", 1}, {"max_results", 9}},
        {0, 0, "今天中东局势如何"});
    const nlohmann::json document = nlohmann::json::parse(result.content);

    EXPECT_EQ(provider.lastRequest.topic, "news");
    EXPECT_EQ(provider.lastRequest.days, 1);
    EXPECT_EQ(provider.lastRequest.maxResults, 3U);
    EXPECT_EQ(document.at("query"), "中东局势");
    EXPECT_TRUE(document.at("requested_at").is_string());
    EXPECT_FALSE(result.terminal);
}

TEST(WebSearchActionTest, AcceptsGeneralTimeRangeAndDefaults)
{
    FakeSearchProvider provider;
    Live<WebSearchOptions> options{WebSearchOptions{}};
    WebSearchAction action(provider, options);

    action.execute({{"query", "glm 发布"}, {"time_range", "week"}}, {});

    EXPECT_EQ(provider.lastRequest.topic, "general");
    ASSERT_TRUE(provider.lastRequest.timeRange.has_value());
    EXPECT_EQ(*provider.lastRequest.timeRange, "week");
    EXPECT_FALSE(provider.lastRequest.days.has_value());
}

TEST(WebSearchActionTest, KeepsUndatedAndStaleResultsAsEvidence)
{
    FakeSearchProvider provider;
    provider.results.push_back({"Fresh", "https://example.com/fresh", "fresh", "2026-08-17", 0.9});
    provider.results.push_back({"Old", "https://example.com/old", "old", "2024-01-01", 0.8});
    provider.results.push_back({"Undated", "https://example.com/undated", "undated", std::nullopt, 0.7});
    provider.answer = "综合摘要：局势近期有变化。";
    Live<WebSearchOptions> options{WebSearchOptions{}};
    WebSearchAction action(provider, options);

    const ActionResult result = action.execute(
        {{"query", "中东局势"}, {"topic", "news"}, {"days", 1}},
        {0, 0, "搜索今天的中东局势"});
    const nlohmann::json document = nlohmann::json::parse(result.content);

    // 时效判断交给模型：结果连同 published_at 原样返回，不在本地丢弃
    EXPECT_EQ(document.at("result_count"), 3);
    EXPECT_EQ(document.at("results").size(), 3U);
    EXPECT_EQ(document.at("answer"), "综合摘要：局势近期有变化。");
    EXPECT_EQ(document.at("results").at(0).at("published_at"), "2026-08-17");
    EXPECT_FALSE(document.at("results").at(2).contains("published_at"));
}

TEST(WebSearchActionTest, RejectsInvalidArgumentsWithoutCallingProvider)
{
    FakeSearchProvider provider;
    Live<WebSearchOptions> options{WebSearchOptions{}};
    WebSearchAction action(provider, options);

    const ActionResult emptyQuery = action.execute({{"query", "  "}}, {});
    EXPECT_TRUE(nlohmann::json::parse(emptyQuery.content).contains("error"));

    const ActionResult badTopic = action.execute(
        {{"query", "x"}, {"topic", "sports"}}, {});
    EXPECT_TRUE(nlohmann::json::parse(badTopic.content).contains("error"));

    const ActionResult badRange = action.execute(
        {{"query", "x"}, {"time_range", "hour"}}, {});
    EXPECT_TRUE(nlohmann::json::parse(badRange.content).contains("error"));

    const ActionResult badDays = action.execute(
        {{"query", "x"}, {"days", 0}}, {});
    EXPECT_TRUE(nlohmann::json::parse(badDays.content).contains("error"));

    EXPECT_TRUE(provider.lastRequest.query.empty());
    EXPECT_FALSE(emptyQuery.terminal);
}

TEST(WebSearchActionTest, TruncatesResultContentToConfiguredLimit)
{
    FakeSearchProvider provider;
    provider.results.push_back({"Docs", "https://example.com", "abcdef", std::nullopt, std::nullopt});
    WebSearchOptions options;
    options.maxContentChars = 4;
    Live<WebSearchOptions> liveOptions(options);
    WebSearchAction action(provider, liveOptions);

    const ActionResult result = action.execute({{"query", "release"}}, {});
    const nlohmann::json document = nlohmann::json::parse(result.content);

    EXPECT_EQ(document.at("results").at(0).at("content"), "abcd…");
}

// 配置实时生效（L1.e）：WebSearchAction 每次执行开头取一份快照。先按 3 条、4 字执行一次，
// 改成 2 条、2 字之后，下一次执行就用新上限
TEST(WebSearchActionTest, NextExecutionUsesReplacedLimits)
{
    FakeSearchProvider provider;
    provider.results.push_back({"Docs", "https://example.com", "abcdef", std::nullopt, std::nullopt});
    WebSearchOptions initial;
    initial.maxResults = 3;
    initial.maxContentChars = 4;
    Live<WebSearchOptions> options(initial);
    WebSearchAction action(provider, options);
    const nlohmann::json arguments = {{"query", "release"}, {"max_results", 9}};

    const nlohmann::json before = nlohmann::json::parse(action.execute(arguments, {}).content);
    EXPECT_EQ(provider.lastRequest.maxResults, 3U);
    EXPECT_EQ(before.at("results").at(0).at("content"), "abcd…");

    WebSearchOptions next = initial;
    next.maxResults = 2;
    next.maxContentChars = 2;
    options.set(next);
    const nlohmann::json after = nlohmann::json::parse(action.execute(arguments, {}).content);
    EXPECT_EQ(provider.lastRequest.maxResults, 2U);
    EXPECT_EQ(after.at("results").at(0).at("content"), "ab…");
}

// 配置实时生效（L2.a）：搜索工具始终注册，是否开放给模型看活值里的 enabled，
// 经 ActionTool 转出；开关一换，下一次问就是新值
TEST(WebSearchActionTest, AvailabilityFollowsLiveEnabledSwitch)
{
    FakeSearchProvider provider;
    WebSearchOptions initial;
    initial.enabled = false;
    Live<WebSearchOptions> options(initial);
    WebSearchAction action(provider, options);
    ActionTool tool(action);
    EXPECT_FALSE(action.available());
    EXPECT_FALSE(tool.available());

    WebSearchOptions next = initial;
    next.enabled = true;
    options.set(next);
    EXPECT_TRUE(action.available());
    EXPECT_TRUE(tool.available());

    options.set(initial);
    EXPECT_FALSE(tool.available());
}

// 配置实时生效（L1.e）：TavilySearchProvider 每次搜索开头取一份快照，端点、密钥、
// 搜索深度改了，下一次搜索即用新值
TEST(TavilySearchProviderTest, NextSearchUsesReplacedOptions)
{
    WebSearchOptions initial;
    initial.endpoint = "https://a.example.test/search";
    initial.apiKey = "key-a";
    initial.searchDepth = "basic";
    Live<WebSearchOptions> options(initial);
    std::vector<std::string> endpoints;
    std::vector<std::string> keys;
    std::vector<std::string> depths;
    TavilySearchProvider provider(
        options, nullptr,
        [&](const std::string &endpoint, const std::string &apiKey, const std::string &payload)
        {
            endpoints.push_back(endpoint);
            keys.push_back(apiKey);
            depths.push_back(nlohmann::json::parse(payload).at("search_depth").get<std::string>());
            return TavilyHttpResponse{200, R"({"results":[]})"};
        });
    SearchRequest request;
    request.query = "query";
    request.maxResults = 3;

    provider.search(request);
    WebSearchOptions next = initial;
    next.endpoint = "https://b.example.test/search";
    next.apiKey = "key-b";
    next.searchDepth = "advanced";
    options.set(next);
    provider.search(request);

    EXPECT_EQ(endpoints, (std::vector<std::string>{"https://a.example.test/search",
                                                   "https://b.example.test/search"}));
    EXPECT_EQ(keys, (std::vector<std::string>{"key-a", "key-b"}));
    EXPECT_EQ(depths, (std::vector<std::string>{"basic", "advanced"}));
}

TEST(TavilySearchProviderTest, BuildsPayloadAndParsesAnswerAndResults)
{
    WebSearchOptions options;
    options.endpoint = "https://search.example.test";
    options.apiKey = "secret";
    options.searchDepth = "advanced";
    std::string receivedEndpoint;
    std::string receivedKey;
    nlohmann::json receivedPayload;
    Live<WebSearchOptions> liveOptions(options);
    TavilySearchProvider provider(
        liveOptions, nullptr,
        [&](const std::string &endpoint, const std::string &apiKey,
            const std::string &payload)
        {
            receivedEndpoint = endpoint;
            receivedKey = apiKey;
            receivedPayload = nlohmann::json::parse(payload);
            return TavilyHttpResponse{
                200,
                R"({"answer":"摘要内容","results":[{"title":"Docs","url":"https://example.com/docs","content":"body","published_date":"2026-08-17","score":0.75}]})"};
        });

    SearchRequest request;
    request.query = "test query";
    request.maxResults = 4;
    request.topic = "news";
    request.days = 1;
    const SearchResponse response = provider.search(request);

    EXPECT_EQ(receivedEndpoint, options.endpoint);
    EXPECT_EQ(receivedKey, "secret");
    EXPECT_EQ(receivedPayload.at("query"), "test query");
    EXPECT_EQ(receivedPayload.at("max_results"), 4);
    EXPECT_EQ(receivedPayload.at("search_depth"), "advanced");
    EXPECT_EQ(receivedPayload.at("include_answer"), true);
    EXPECT_EQ(receivedPayload.at("topic"), "news");
    EXPECT_EQ(receivedPayload.at("days"), 1);
    EXPECT_EQ(response.answer, "摘要内容");
    ASSERT_EQ(response.results.size(), 1U);
    EXPECT_EQ(response.results.front().title, "Docs");
    EXPECT_EQ(response.results.front().publishedAt, "2026-08-17");
}

TEST(TavilySearchProviderTest, ConvertsApiErrorIntoException)
{
    WebSearchOptions options;
    Live<WebSearchOptions> liveOptions(options);
    TavilySearchProvider provider(
        liveOptions, nullptr,
        [](const std::string &, const std::string &, const std::string &)
        {
            return TavilyHttpResponse{401, R"({"detail":{"error":"invalid key"}})"};
        });

    SearchRequest request;
    request.query = "query";
    request.maxResults = 5;
    EXPECT_THROW(provider.search(request), std::runtime_error);
}

TEST(WebSearchConfigTest, DisablesFeatureWhenKeyIsMissing)
{
    const std::string config = R"({
        "schema_version": 1,
        "bot": {"id": 1},
        "chat": {"default_model": "test"},
        "models": {"registry_path": "models.json"},
        "web_search": {"enabled": true},
        "communication": {
            "protocol": {"type": "onebot"},
            "active_transport": "local",
            "transports": {"local": {"type": "reverse_websocket", "bind": "127.0.0.1", "port": 8600}}
        }
    })";

    const ConfigLoadResult result = ConfigLoader().loadText(config);

    ASSERT_TRUE(result.canStart());
    EXPECT_FALSE(result.config->webSearch.enabled);
}

TEST(WebSearchConfigTest, MapsEnabledSearchSettingsIntoRuntimeOptions)
{
    const std::string config = R"({
        "schema_version": 1,
        "bot": {"id": 1},
        "chat": {"default_model": "test"},
        "models": {"registry_path": "models.json"},
        "network": {"proxy": "http://127.0.0.1:7890"},
        "web_search": {
            "enabled": true,
            "api_key": {"literal": "test-key"},
            "search_depth": "advanced",
            "max_results": 7,
            "max_content_chars": 3000
        },
        "communication": {
            "protocol": {"type": "onebot"},
            "active_transport": "local",
            "transports": {"local": {"type": "reverse_websocket", "bind": "127.0.0.1", "port": 8600}}
        }
    })";

    const ConfigLoadResult loaded = ConfigLoader().loadText(config);
    ASSERT_TRUE(loaded.canStart());
    const RuntimeSettings runtime = buildRuntimeSettings(*loaded.config);

    EXPECT_TRUE(runtime.webSearch.enabled);
    EXPECT_EQ(runtime.webSearch.apiKey, "test-key");
    EXPECT_EQ(runtime.webSearch.searchDepth, "advanced");
    EXPECT_EQ(runtime.webSearch.maxResults, 7U);
    EXPECT_EQ(runtime.webSearch.maxContentChars, 3000U);
    EXPECT_EQ(runtime.webSearch.proxy, "http://127.0.0.1:7890");
}

TEST(TavilySearchProviderIntegrationTest, SearchesRealApiWhenEnvironmentIsConfigured)
{
    const char *apiKey = std::getenv("KLEIN_TAVILY_API_KEY");
    if (apiKey == nullptr || std::string(apiKey).empty())
        GTEST_SKIP() << "KLEIN_TAVILY_API_KEY is not configured";

    WebSearchOptions options;
    options.apiKey = apiKey;
    options.maxResults = 3;
    Live<WebSearchOptions> liveOptions(options);
    TavilySearchProvider provider(liveOptions);

    SearchRequest request;
    request.query = "C++23 compiler support";
    request.maxResults = options.maxResults;
    const SearchResponse response = provider.search(request);

    EXPECT_FALSE(response.results.empty());
    EXPECT_FALSE(response.results.front().title.empty());
    EXPECT_FALSE(response.results.front().url.empty());
}
