#include <gtest/gtest.h>

#include "Application/ToolPolicyFilter.h"

#include <string>
#include <vector>

namespace
{
const std::vector<std::string> kOffered{"inspect_image", "generate_image", "klein_web_search"};

affect::BehaviorPlan planWith(const std::string &toolPolicy,
                              std::vector<std::string> allowedTools = {})
{
    affect::BehaviorPlan plan;
    plan.action = "group_say";
    plan.toolPolicy = toolPolicy;
    plan.allowedTools = std::move(allowedTools);
    return plan;
}

TEST(ToolPolicyFilterTest, InheritKeepsOfferedAsIs)
{
    EXPECT_EQ(filterToolNames(kOffered, planWith("inherit")), kOffered);
}

TEST(ToolPolicyFilterTest, AllowListKeepsOfferedOrderAndDropsUnoffered)
{
    const auto kept = filterToolNames(
        kOffered, planWith("allow_list", {"klein_web_search", "send_image", "inspect_image"}));
    EXPECT_EQ(kept, (std::vector<std::string>{"inspect_image", "klein_web_search"}));
}

TEST(ToolPolicyFilterTest, NoneRemovesAllRegistryTools)
{
    EXPECT_TRUE(filterToolNames(kOffered, planWith("none")).empty());
}
} // namespace
