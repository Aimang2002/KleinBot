#ifndef MESSAGETASK_H
#define MESSAGETASK_H

#include "../JsonParse/JsonParse.h"
#include "../ConfigManager/ConfigManager.h"
#include <queue>
#include <string>
#include <mutex>
#include <memory>
#include <optional>
#include <iostream>

class MessageQueue
{
public:
    MessageQueue() {}

    // 消息入列
    static void original_push_queue(std::string task);

    // 入队已封装好的 WebSocket payload（完整的 {"action":..., "params":...} 字符串）
    // 由 QQMessageSender 调用
    static void pending_push_raw(const std::string &packed);

    // 原子出队：单次加锁内完成 空判断+取首+弹出，空队列返回 nullopt
    static std::optional<std::string> original_try_pop();
    static std::optional<std::string> pending_try_pop();

private:
    static std::unique_ptr<std::queue<std::string>> origina_queue; // 原始消息队列
    static std::mutex original_mutex;                              // 原始锁

    static std::unique_ptr<std::queue<std::string>> pending_queue; // 待发送消息队列
    static std::mutex pending_mutex;                               // 待发送锁
};

#endif // MESSAGETASK_H