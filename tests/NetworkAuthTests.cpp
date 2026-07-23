#include <gtest/gtest.h>

#include "Network/WebSocketAuth.h"

TEST(WebSocketAuthTest, BuildsBearerAuthorizationValue)
{
    EXPECT_EQ(WebSocketAuth::buildAuthorizationValue("secret-token"), "Bearer secret-token");
}

TEST(WebSocketAuthTest, AllowsConnectionsWhenTokenIsNotConfigured)
{
    EXPECT_TRUE(WebSocketAuth::isAuthorized("", ""));
    EXPECT_TRUE(WebSocketAuth::isAuthorized("Bearer any-token", ""));
}

TEST(WebSocketAuthTest, AcceptsCorrectBearerToken)
{
    EXPECT_TRUE(WebSocketAuth::isAuthorized("Bearer secret-token", "secret-token"));
    EXPECT_TRUE(WebSocketAuth::isAuthorized("bearer secret-token", "secret-token"));
}

TEST(WebSocketAuthTest, RejectsMissingOrIncorrectBearerToken)
{
    EXPECT_FALSE(WebSocketAuth::isAuthorized("", "secret-token"));
    EXPECT_FALSE(WebSocketAuth::isAuthorized("Basic secret-token", "secret-token"));
    EXPECT_FALSE(WebSocketAuth::isAuthorized("Bearer wrong-token", "secret-token"));
    EXPECT_FALSE(WebSocketAuth::isAuthorized("Bearer secret-token-extra", "secret-token"));
}

TEST(WebSocketAuthTest, RejectsTokensWithLargeLengthDifference)
{
    const std::string expectedToken(300, 'a');
    const std::string presentedToken = expectedToken + std::string(256, 'b');

    EXPECT_FALSE(WebSocketAuth::isAuthorized("Bearer " + presentedToken, expectedToken));
}
