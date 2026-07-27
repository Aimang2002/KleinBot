#ifndef CHATRESPONSE_H
#define CHATRESPONSE_H

#include <string>
#include <vector>

// 模型请求调用的单个工具
struct ResponseToolCall
{
    std::string id;
    std::string name;
    std::string arguments; // JSON 字符串
};

struct ChatResponse
{
    bool cancelled = false;
    int code = 0;              // HTTP 状态码（200=成功，其余视为失败）
    std::string content;       // 助手回复文本
    std::string finish_reason; // "stop" / "length" / "tool_calls" / "content_filter" 等

    std::vector<ResponseToolCall> tool_calls; // finish_reason=="tool_calls" 时非空

    int input_tokens = 0;
    int output_tokens = 0;
    int total_tokens = 0;

    std::string error_message; // 失败时的错误描述（成功时为空）
};

#endif // CHATRESPONSE_H
