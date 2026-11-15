#ifndef PERSON_H
#define PERSON_H

// #include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <cstdint>
#include <optional>

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

    // 滚动摘要：锚点前移时被移出窗口的原文经杂务模型合并成的"早前对话摘要"，
    // 由 ChatService 前插进请求（见 Application/ContextDigest）。只在内存：
    // 用户 2026-11-08 决定不落库（落库需要给会话表加列，待议），重启即空。
    // #重置对话/#重置上下文/#人格还原 清空；#删除上条对话（撤回）不动
    std::string context_digest;

    // 会话设定
    std::string system_prompt;

    // 用户手动选的模型（#切换模型）；空 = 没选过，跟随当前默认模型 chat.default_model
    // （用户定规 2026-11-15：默认模型改了老用户跟）。实际用哪个模型一律经
    // resolveModelName（UserSessionService.h）解析。只在内存，重启即空
    std::optional<std::string> current_model;

    // 其他信息
    bool isOpenVoiceMode = false; // 是否开启语音模式，默认为否
};

#endif
