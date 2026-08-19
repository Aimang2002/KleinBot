#include <gtest/gtest.h>

#include "Network/BearerAuth.h"
#include "Network/WebhookSignature.h"

TEST(BearerAuthTest, BuildsBearerAuthorizationValue)
{
    EXPECT_EQ(BearerAuth::buildAuthorizationValue("secret-token"), "Bearer secret-token");
}

TEST(BearerAuthTest, AllowsConnectionsWhenTokenIsNotConfigured)
{
    EXPECT_TRUE(BearerAuth::isAuthorized("", ""));
    EXPECT_TRUE(BearerAuth::isAuthorized("Bearer any-token", ""));
}

TEST(BearerAuthTest, AcceptsCorrectBearerToken)
{
    EXPECT_TRUE(BearerAuth::isAuthorized("Bearer secret-token", "secret-token"));
    EXPECT_TRUE(BearerAuth::isAuthorized("bearer secret-token", "secret-token"));
}

TEST(BearerAuthTest, RejectsMissingOrIncorrectBearerToken)
{
    EXPECT_FALSE(BearerAuth::isAuthorized("", "secret-token"));
    EXPECT_FALSE(BearerAuth::isAuthorized("Basic secret-token", "secret-token"));
    EXPECT_FALSE(BearerAuth::isAuthorized("Bearer wrong-token", "secret-token"));
    EXPECT_FALSE(BearerAuth::isAuthorized("Bearer secret-token-extra", "secret-token"));
}

TEST(BearerAuthTest, RejectsTokensWithLargeLengthDifference)
{
    const std::string expectedToken(300, 'a');
    const std::string presentedToken = expectedToken + std::string(256, 'b');

    EXPECT_FALSE(BearerAuth::isAuthorized("Bearer " + presentedToken, expectedToken));
}

TEST(WebhookSignatureTest, BuildsOneBotHmacSha1Signature)
{
    EXPECT_EQ(
        WebhookSignature::signSha1(
            "The quick brown fox jumps over the lazy dog", "key"),
        "sha1=de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9");
}

TEST(WebhookSignatureTest, AcceptsCorrectSignatureAndRejectsInvalidValues)
{
    const std::string body = R"({"post_type":"message"})";
    const std::string signature = WebhookSignature::signSha1(body, "secret");

    EXPECT_TRUE(WebhookSignature::isAuthorized(signature, body, "secret"));
    EXPECT_FALSE(WebhookSignature::isAuthorized(signature, body + " ", "secret"));
    EXPECT_FALSE(WebhookSignature::isAuthorized("sha1=invalid", body, "secret"));
    EXPECT_TRUE(WebhookSignature::isAuthorized("", body, ""));
}
