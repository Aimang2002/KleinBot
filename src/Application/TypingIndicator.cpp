#include "TypingIndicator.h"

#include <chrono>

TypingIndicator::TypingIndicator(const CapabilityBroker &capabilities, OneBotApiChannel &api)
    : capabilities_(capabilities), api_(api)
{
}

void TypingIndicator::begin(std::uint64_t userId)
{
    if (!capabilities_.supports(BotFeature::InputStatus))
    {
        return;
    }
    nlohmann::json params;
    params["user_id"] = userId;
    // 返回值有意丢弃：失败（超时/不支持/限流）静默，不影响回复主链路
    (void)api_.call("set_input_status", std::move(params), std::chrono::seconds(1));
}
