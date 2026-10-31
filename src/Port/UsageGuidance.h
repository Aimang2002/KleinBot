#ifndef USAGE_GUIDANCE_H
#define USAGE_GUIDANCE_H

/*
 * 工具使用规则的场景与调用期上下文（ADR-022 机制复用）。
 * 每个工具（Tool::usageGuidance / Action::usageGuidance）自己给出"何时该用我"的规则文本，
 * Application/ToolGuidance 的 composeToolGuidance 按固定顺序拼进 system prompt。
 * 放在 Port：Tool 与 Action 两层都要认识它，彼此又不互相依赖。
 */
#include <string>

enum class GuidanceScene
{
    Conversation, // 一对一对话（ChatService::reply 的 system prompt）
    GroupTurn,    // 群话题会话轮（EngagementService::runTurn 的会话契约）
};

struct GuidanceContext
{
    std::string localDate; // 运行机器本地日期 YYYY-MM-DD（时效类规则的时间锚点）
};

#endif // USAGE_GUIDANCE_H
