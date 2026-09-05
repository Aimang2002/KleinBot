#ifndef TYPING_INDICATOR_H
#define TYPING_INDICATOR_H

#include "CapabilityBroker.h"
#include "../Network/OneBotApiChannel.h"

#include <cstdint>

// 私聊"对方正在输入"提示（T5）：LLM 调用前触发一次，能力位门控（D12——
// 消费方只问 supports，实现端差异不进业务）。fire-and-forget：1s 超时、
// 结果静默丢弃，绝不阻塞回复路径；不发送取消——回复消息到达或客户端超时
// 自然清除（取消语义各实现端不一，暂不引入）
class TypingIndicator
{
public:
    TypingIndicator(const CapabilityBroker &capabilities, OneBotApiChannel &api);

    // 仅在私聊聊天路径调用；群聊/命令/错误提示路径不应触达
    void begin(std::uint64_t userId);

private:
    const CapabilityBroker &capabilities_;
    OneBotApiChannel &api_;
};

#endif // TYPING_INDICATOR_H
