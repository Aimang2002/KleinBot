#include <gtest/gtest.h>

#include "ModelApiCaller/OpenAIStandard/OpenAIStandard.h"

// 生图响应解析（真机 2026-10-30：中转站默认只回 url，不回 b64_json）
TEST(OpenAIImageParseTest, ReadsB64OrFallsBackToUrl)
{
    OpenAIStandard standard;

    const ImageResponse b64 = standard.draw_json_parse(
        R"({"created":1,"data":[{"b64_json":"aGVsbG8="}],"usage":{"input_tokens":3,"output_tokens":5,"total_tokens":8}})");
    EXPECT_EQ(b64.image_base64, "aGVsbG8=");
    EXPECT_TRUE(b64.image_url.empty());
    EXPECT_EQ(b64.total_tokens, 8);

    const ImageResponse prefixed = standard.draw_json_parse(
        R"({"data":[{"b64_json":"data:image/png;base64,aGVsbG8="}]})");
    EXPECT_EQ(prefixed.image_base64, "aGVsbG8=") << "剥掉 data URI 前缀";

    const ImageResponse urlOnly = standard.draw_json_parse(
        R"({"created":1,"data":[{"url":"https://cdn.example/x.png"}],"task_id":"t"})");
    EXPECT_TRUE(urlOnly.image_base64.empty());
    EXPECT_EQ(urlOnly.image_url, "https://cdn.example/x.png");

    const ImageResponse error = standard.draw_json_parse(
        R"({"error":{"message":"Model \"gpt-image-2\" is not available for this group","type":"invalid_request_error"}})");
    EXPECT_TRUE(error.image_base64.empty());
    EXPECT_TRUE(error.image_url.empty());
    EXPECT_NE(error.error_message.find("not available"), std::string::npos);

    EXPECT_TRUE(standard.draw_json_parse("").image_url.empty());
    EXPECT_TRUE(standard.draw_json_parse("not json").image_base64.empty());
}
