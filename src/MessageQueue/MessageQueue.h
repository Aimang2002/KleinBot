#ifndef MESSAGETASK_H
#define MESSAGETASK_H

#include "../JsonParse/JsonParse.h"
#include "../ConfigManager/ConfigManager.h"
#include <queue>
#include <string>
#include <mutex>
#include <memory>
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

    // 判断消息队列是否为空
    static bool original_empty();
    static bool pending_empty();

    // 获取第一个消息
    static std::string original_front_queue();
    static std::string pending_front_queue();

    // 弹出第一个消息
    static bool original_pop();
    static bool pending_pop();

private:
    static std::unique_ptr<std::queue<std::string>> origina_queue; // 原始消息队列
    static std::mutex original_mutex;                              // 原始锁

    static std::unique_ptr<std::queue<std::string>> pending_queue; // 待发送消息队列
    static std::mutex pending_mutex;                               // 待发送锁
};

#endif // MESSAGETASK_H