#include <gtest/gtest.h>

#include "Network/BearerAuth.h"

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
