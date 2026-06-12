#ifndef CHATREQUEST_H
#define CHATREQUEST_H

#include <vector>
#include <string>

// 单条工具调用请求（assistant 决定要调的工具）
struct ToolCallRequest
{
    std::string id;        // tool_call id，结果回传时配对用
    std::string name;      // 工具名
    std::string arguments; // JSON 字符串形式的参数
};

struct ChatMessage
{
    std::string role; // "system"/"user"/"assistant"/"tool"
    std::string content;

    // role=="tool" 时填：把工具结果回传给模型
    std::string tool_call_id;

    // role=="assistant" 且模型要调工具时填：本条消息携带的工具调用
    std::vector<ToolCallRequest> tool_calls;
};

struct ChatRequest
{
    std::string system_prompt;
    std::vector<ChatMessage> history;

    // 可用工具的 schema 列表，每项是一个完整的 OpenAI tool 定义 JSON 字符串
    // 空 = 本次请求不带工具（行为与扩展前完全一致）
    std::vector<std::string> tools;

    // 超参数（可选）
    double temperature = 0.7;
    double frequency_penalty = 0.0;
    double presence_penalty = 0.0;
    int max_tokens = 0;
};

#endif // CHATREQUEST_H
