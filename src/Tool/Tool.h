#ifndef TOOL_H
#define TOOL_H

#include <string>
#include <vector>
#include "ToolContext.h"
#include "../Port/OutboundMessage.h"
#include "../Port/UsageGuidance.h"

struct ToolResult
{
    std::string model_content;
    std::vector<OutboundMessage> outbound_messages;
    std::string context_content;
    bool terminal = false;
    bool cancelled = false;
    bool suppress_text_reply = false;
};

inline bool terminatesToolRound(const ToolResult &result)
{
    return result.terminal || !result.outbound_messages.empty();
}

// 工具抽象：模型可自主调用的能力单元
// 与 Command（用户关键词触发）并存，互不知道对方
class Tool
{
public:
    virtual ~Tool() = default;

    // 工具名，必须唯一，模型用它指定要调哪个
    virtual std::string name() const = 0;

    // 给模型看的自然语言描述：这个工具干什么、何时该用
    virtual std::string description() const = 0;

    // 参数的 JSON Schema 字符串（OpenAI parameters 对象）
    // 无参数的工具返回空对象 schema
    virtual std::string parametersSchema() const = 0;

    virtual bool requiresAdmin() const { return false; }

    // 工具自带的使用规则（何时该用我、用完怎么做），由 Application/ToolGuidance 的
    // composeToolGuidance 按场景、按固定顺序拼进 system prompt；没有规则返回空串。
    // 文本必须稳定（只随 context 里的日期等运行时锚点变化）：它位于前缀缓存最前端
    virtual std::string usageGuidance(GuidanceScene, const GuidanceContext &) const { return {}; }

    // 执行工具。文本回灌模型，出站消息交给消息适配器发送。
    virtual ToolResult execute(const std::string &args, const ToolContext &ctx) = 0;
};

#endif // TOOL_H
