#ifndef PERSON_H
#define PERSON_H

// #include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <cstdint>

struct TimestampedMessage
{
    std::string role;
    std::string content;
    time_t timestamp = 0;
    int64_t id = 0;
    double relevance = 0.0;
};

// 用户类
struct Person
{
    // 历史对话
    std::vector<TimestampedMessage> user_chatHistory; // 用户聊天信息和时间戳

    // 会话设定
    std::string system_prompt;
    double temperature = 0.7;       // 温度
    double frequency_penalty = 0.0; // 频率惩罚
    double presence_penalty = 0.0;  // 存在惩罚

    // 用户当前模型
    std::string current_model;

    // 其他信息
    bool isOpenVoiceMode = false; // 是否开启语音模式，默认为否
};

#endif
