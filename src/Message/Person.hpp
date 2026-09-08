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

    // 上下文裁切锚点：当前请求窗口在存活消息列表中的起始下标。
    // 只有窗口超过高水位时才整体前移，两次移动之间头部逐字节稳定，
    // 供应商前缀缓存才能跨请求命中（见 UserSessionService::buildChatRequest）
    std::size_t history_anchor = 0;

    // 会话设定
    std::string system_prompt;
    // 人格编译请求（T5）：#重置对话 后置位，首次聊天由 AI 按 persona-spec
    // 把 soul.md 编译为标签式 system_prompt 并清位。手动人格（#设置人格）
    // 存在时不编译。仅内存状态，不持久化
    bool persona_needs_build = false;
    double temperature = 0.7;       // 温度
    double frequency_penalty = 0.0; // 频率惩罚
    double presence_penalty = 0.0;  // 存在惩罚

    // 用户当前模型
    std::string current_model;

    // 其他信息
    bool isOpenVoiceMode = false; // 是否开启语音模式，默认为否
};

#endif
