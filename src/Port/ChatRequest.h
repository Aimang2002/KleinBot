#ifndef CHATREQUEST_H
#define CHATREQUEST_H

#include <vector>
#include <string>

struct ChatMessage
{
    std::string role;
    std::string content;
};

struct ChatRequest
{
    std::string system_prompt;
    std::vector<ChatMessage> history;

    // 超参数（可选）
    double temperature = 0.7;
    double frequency_penalty = 0.0;
    double presence_penalty = 0.0;
    int max_tokens = 0;
};

#endif // CHATREQUEST_H