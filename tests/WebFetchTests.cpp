#include <gtest/gtest.h>

#include "Action/WebFetchAction.h"
#include "Bootstrap/RuntimeSettings.h"
#include "Configuration/ConfigLoader.h"
#include "WebFetch/HtmlTextExtractor.h"
#include <array>
#include <cstdlib>
#include <string>

namespace
{
std::string baseConfigWith(const std::string &extra)
{
    return R"({
        "schema_version": 1,
        "bot": {"id": 1},
        "chat": {"default_model": "test"},
        "models": {"registry_path": "models.json"},
        "resources": {"help_file": "source/help.txt"},
        )" + extra + R"(
        "communication": {
            "protocol": {"type": "onebot"},
            "active_transport": "local",
            "transports": {"local": {"type": "reverse_websocket", "bind": "127.0.0.1", "port": 8600}}
        }
    })";
}
}

TEST(HtmlTextExtractorTest, ExtractsTitleAndDropsPageNoise)
{
    const std::string html = R"(<!DOCTYPE html>
<html><head><title>示例页面 &amp; 说明</title><meta charset="utf-8">
<style>.hidden{color:red}</style><script>if (1 < 2) alert("x");</script></head>
<body><nav><a>首页</a><a>菜单</a></nav>
<h1>标题一</h1><p>第一段&amp;内容。</p>
<h2>标题二</h2><ul><li>项目一</li><li>项目二</li></ul>
<footer>版权所有</footer></body></html>)";

    const HtmlTextExtraction extraction = extractHtmlText(html);

    EXPECT_EQ(extraction.title, "示例页面 & 说明");
    EXPECT_EQ(extraction.text.find("color:red"), std::string::npos);
    EXPECT_EQ(extraction.text.find("alert"), std::string::npos);
    EXPECT_EQ(extraction.text.find("首页"), std::string::npos);
    EXPECT_EQ(extraction.text.find("版权所有"), std::string::npos);
    EXPECT_NE(extraction.text.find("# 标题一"), std::string::npos);
    EXPECT_NE(extraction.text.find("## 标题二"), std::string::npos);
    EXPECT_NE(extraction.text.find("- 项目一"), std::string::npos);
    EXPECT_NE(extraction.text.find("第一段&内容。"), std::string::npos);
}

TEST(HtmlTextExtractorTest, DecodesNumericEntitiesToUtf8)
{
    const HtmlTextExtraction extraction =
        extractHtmlText("<p>&#20013;&#x6587; &#8212; 完成</p>");
    EXPECT_EQ(extraction.text, "中文 — 完成");
}

TEST(HtmlTextExtractorTest, DetectsDeclaredCharset)
{
    EXPECT_EQ(detectHtmlCharset(
                  R"(<html><head><meta http-equiv="Content-Type" content="text/html; charset=GBK">)"),
              "gbk");
    EXPECT_EQ(detectHtmlCharset(R"(<html><head><meta charset="utf-8">)"), "utf-8");
    EXPECT_EQ(detectHtmlCharset("<html><body></body></html>"), "");
}

namespace
{
class FakeFetchFixture
{
public:
    WebFetchAction::HttpGet handler()
    {
        return [this](const std::string &url)
        {
            ++fetchCount;
            lastUrl = url;
            return respond(url);
        };
    }

    virtual PageHttpResponse respond(const std::string &url) = 0;

    int fetchCount = 0;
    std::string lastUrl;
};

class StaticPageFetch : public FakeFetchFixture
{
public:
    PageHttpResponse respond(const std::string &url) override
    {
        PageHttpResponse response;
        response.statusCode = statusCode;
        response.contentType = contentType;
        response.effectiveUrl = effectiveUrl.empty() ? url : effectiveUrl;
        response.body = body;
        return response;
    }

    long statusCode = 200;
    std::string contentType = "text/html; charset=utf-8";
    std::string effectiveUrl;
    std::string body = "<html><head><title>文章</title></head><body>"
                       "<h1>大标题</h1><p>正文第一段。</p></body></html>";
};
}

TEST(WebFetchActionTest, ReturnsDirectContentWithMetadata)
{
    StaticPageFetch fetch;
    WebFetchAction action(WebFetchOptions{}, nullptr, fetch.handler());

    const ActionResult result = action.execute(
        {{"url", "https://example.com/post/1"}}, {0, 0, "看看这篇文章"});
    const nlohmann::json document = nlohmann::json::parse(result.content);

    EXPECT_EQ(document.at("status"), 200);
    EXPECT_EQ(document.at("title"), "文章");
    EXPECT_EQ(document.at("method"), "direct");
    EXPECT_EQ(document.at("truncated"), false);
    EXPECT_EQ(document.at("cached"), false);
    EXPECT_EQ(document.at("content_type"), "text/html");
    EXPECT_NE(document.at("content").get<std::string>().find("正文第一段"),
              std::string::npos);
    EXPECT_FALSE(result.terminal);
    EXPECT_TRUE(result.context_content.empty());
    EXPECT_EQ(fetch.fetchCount, 1);
}

TEST(WebFetchActionTest, RejectsInvalidUrlsBeforeAnyRequest)
{
    StaticPageFetch fetch;
    WebFetchAction action(WebFetchOptions{}, nullptr, fetch.handler());

    const std::array<std::string, 8> invalid{
        "", "ftp://example.com/file", "javascript:alert(1)", "not-a-url",
        "http://localhost/admin", "http://127.0.0.1:8080/",
        "https://192.168.1.1/", "http://user@10.0.0.5/x"};
    for (const std::string &url : invalid)
    {
        const ActionResult result = action.execute({{"url", url}}, {});
        EXPECT_TRUE(nlohmann::json::parse(result.content).contains("error")) << url;
    }
    EXPECT_EQ(fetch.fetchCount, 0);
}

TEST(WebFetchActionTest, DistillsLongContentAroundProvidedQuestion)
{
    WebFetchOptions options;
    options.maxContentChars = 100;
    StaticPageFetch fetch;
    fetch.body = "<html><body><p>" + std::string(300, 'x') +
                 " 关键结论：售价 399 元，2026-08-18 发布。</p></body></html>";
    std::string capturedPrompt;
    WebFetchAction action(
        options, nullptr, fetch.handler(),
        [&capturedPrompt](const std::string &prompt)
        {
            capturedPrompt = prompt;
            return "关键结论：售价 399 元，2026-08-18 发布。";
        });

    const ActionResult result = action.execute(
        {{"url", "https://example.com/item"}, {"question", "价格是多少"}},
        {0, 0, "用户随口说的话"});
    const nlohmann::json document = nlohmann::json::parse(result.content);

    EXPECT_EQ(document.at("method"), "distilled");
    EXPECT_EQ(document.at("content"), "关键结论：售价 399 元，2026-08-18 发布。");
    EXPECT_EQ(document.at("truncated"), false);
    EXPECT_NE(capturedPrompt.find("价格是多少"), std::string::npos);
    EXPECT_NE(capturedPrompt.find("不可信的外部数据"), std::string::npos);
    EXPECT_NE(capturedPrompt.find("关键结论"), std::string::npos);
}

TEST(WebFetchActionTest, FallsBackToUtf8SafeTruncationWithoutDistiller)
{
    WebFetchOptions options;
    options.maxContentChars = 100;
    StaticPageFetch fetch;
    std::string chinese;
    for (int index = 0; index < 120; ++index)
        chinese += "中";
    fetch.body = "<html><body><p>" + chinese + "</p></body></html>";
    WebFetchAction action(options, nullptr, fetch.handler());

    const ActionResult result = action.execute(
        {{"url", "https://example.com/long"}}, {0, 0, "总结一下"});
    const nlohmann::json document = nlohmann::json::parse(result.content);

    EXPECT_EQ(document.at("method"), "truncated");
    EXPECT_EQ(document.at("truncated"), true);
    const std::string content = document.at("content");
    EXPECT_EQ(content.size(), 102U); // 33 个三字节汉字 + 三字节省略号
    EXPECT_EQ(content.substr(content.size() - 3), "…");
}

TEST(WebFetchActionTest, CachesFetchedPageAcrossQuestionsAndFragments)
{
    WebFetchOptions options;
    options.cacheTtlSeconds = 900;
    options.cacheMaxEntries = 8;
    StaticPageFetch fetch;
    WebFetchAction action(options, nullptr, fetch.handler());

    const ActionResult first = action.execute(
        {{"url", "https://example.com/page"}}, {0, 0, "第一问"});
    const ActionResult second = action.execute(
        {{"url", "https://example.com/page#section"}}, {0, 0, "第二问"});

    EXPECT_EQ(fetch.fetchCount, 1);
    EXPECT_EQ(nlohmann::json::parse(first.content).at("cached"), false);
    EXPECT_EQ(nlohmann::json::parse(second.content).at("cached"), true);
}

TEST(WebFetchActionTest, ReportsHttpAndContentTypeErrors)
{
    {
        StaticPageFetch fetch;
        fetch.statusCode = 404;
        WebFetchAction action(WebFetchOptions{}, nullptr, fetch.handler());
        const ActionResult result = action.execute(
            {{"url", "https://example.com/missing"}}, {});
        const nlohmann::json document = nlohmann::json::parse(result.content);
        EXPECT_TRUE(document.contains("error"));
        EXPECT_NE(document.at("error").get<std::string>().find("404"),
                  std::string::npos);
    }
    {
        StaticPageFetch fetch;
        fetch.contentType = "image/png";
        fetch.body = "\x89PNG...";
        WebFetchAction action(WebFetchOptions{}, nullptr, fetch.handler());
        const ActionResult result = action.execute(
            {{"url", "https://example.com/pic.png"}}, {});
        const nlohmann::json document = nlohmann::json::parse(result.content);
        EXPECT_TRUE(document.contains("error"));
        EXPECT_NE(document.at("error").get<std::string>().find("不支持"),
                  std::string::npos);
    }
}

TEST(WebFetchActionTest, RejectsRedirectsToInternalAddresses)
{
    StaticPageFetch fetch;
    fetch.effectiveUrl = "http://127.0.0.1:8080/secret";
    WebFetchAction action(WebFetchOptions{}, nullptr, fetch.handler());

    const ActionResult result = action.execute(
        {{"url", "https://public.example.com/redirect"}}, {});
    const nlohmann::json document = nlohmann::json::parse(result.content);

    EXPECT_TRUE(document.contains("error"));
    EXPECT_NE(document.at("error").get<std::string>().find("重定向"),
              std::string::npos);
}

TEST(WebFetchActionTest, PassesNonHtmlTextThroughAsEvidence)
{
    StaticPageFetch fetch;
    fetch.contentType = "application/json";
    fetch.body = R"({"items":[{"name":"Widget","price":399}]})";
    WebFetchAction action(WebFetchOptions{}, nullptr, fetch.handler());

    const ActionResult result = action.execute(
        {{"url", "https://api.example.com/items"}}, {0, 0, "多少钱"});
    const nlohmann::json document = nlohmann::json::parse(result.content);

    EXPECT_EQ(document.at("method"), "direct");
    EXPECT_EQ(document.at("content_type"), "application/json");
    EXPECT_EQ(document.at("content"), fetch.body);
}

TEST(WebFetchActionTest, FlagsNonUtf8CharsetForTheModel)
{
    StaticPageFetch fetch;
    fetch.contentType = "text/html";
    fetch.body = "<html><head><meta charset=\"gbk\"><title>页</title></head>"
                 "<body><p>内容</p></body></html>";
    WebFetchAction action(WebFetchOptions{}, nullptr, fetch.handler());

    const ActionResult result = action.execute(
        {{"url", "https://example.com/cn"}}, {});
    const nlohmann::json document = nlohmann::json::parse(result.content);

    EXPECT_EQ(document.at("charset"), "gbk");
    EXPECT_NE(document.at("content").get<std::string>().find("内容"),
              std::string::npos);
}

TEST(WebFetchConfigTest, MapsWebFetchSettingsIntoRuntimeOptions)
{
    const std::string config = baseConfigWith(R"(
        "network": {"proxy": "http://127.0.0.1:7890"},
        "web_fetch": {
            "enabled": true,
            "max_content_chars": 8000,
            "request_timeout_ms": 30000,
            "cache_ttl_seconds": 60,
            "cache_max_entries": 8
        },)");

    const ConfigLoadResult loaded = ConfigLoader().loadText(config);
    ASSERT_TRUE(loaded.canStart());
    const RuntimeSettings runtime = buildRuntimeSettings(*loaded.config);

    EXPECT_TRUE(runtime.webFetch.enabled);
    EXPECT_EQ(runtime.webFetch.maxContentChars, 8000U);
    EXPECT_EQ(runtime.webFetch.requestTimeoutMs, 30000);
    EXPECT_EQ(runtime.webFetch.cacheTtlSeconds, 60);
    EXPECT_EQ(runtime.webFetch.cacheMaxEntries, 8U);
    EXPECT_EQ(runtime.webFetch.proxy, "http://127.0.0.1:7890");
}

TEST(WebFetchConfigTest, KeepsWebFetchDisabledByDefault)
{
    const ConfigLoadResult loaded = ConfigLoader().loadText(baseConfigWith(""));
    ASSERT_TRUE(loaded.canStart());
    EXPECT_FALSE(buildRuntimeSettings(*loaded.config).webFetch.enabled);
}

TEST(WebFetchIntegrationTest, FetchesRealPageWhenEnvironmentIsConfigured)
{
    const char *target = std::getenv("KLEIN_WEB_FETCH_TEST_URL");
    if (target == nullptr || std::string(target).empty())
        GTEST_SKIP() << "KLEIN_WEB_FETCH_TEST_URL is not configured";

    WebFetchAction action(WebFetchOptions{});
    const ActionResult result = action.execute({{"url", target}}, {0, 0, "页面主要内容"});
    const nlohmann::json document = nlohmann::json::parse(result.content);

    EXPECT_FALSE(document.contains("error"));
    EXPECT_GT(document.at("extracted_chars"), 0U);
}
