#ifndef CHATRESPONSE_H
#define CHATRESPONSE_H

#include <string>

struct ChatResponse
{
    int code = 0;                  // HTTP 状态码（200=成功，其余视为失败）
    std::string content;           // 助手回复文本
    std::string finish_reason;     // "stop" / "length" / "content_filter" 等

    int input_tokens = 0;
    int output_tokens = 0;
    int total_tokens = 0;

    std::string error_message;     // 失败时的错误描述（成功时为空）
};

#endif // CHATRESPONSE_H
