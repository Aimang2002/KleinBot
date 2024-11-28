#ifndef MESSAGETASK_H
#define MESSAGETASK_H

#include <iostream>
#include <queue>
#include <string>
#include <sstream>
#include <mutex>
#include "../JsonParse/JsonParse.h"
#include "../ConfigManager/ConfigManager.h"
#include "../Network/MyReverseWebSocket.h"
#include "src/Log/Log.h"

extern JsonParse &JParsingClass;
extern ConfigManager &CManager;

class MessageQueue
{
public:
    MessageQueue() {}

    // 消息入列
    static void original_push_queue(std::string task);
    static void pending_push_queue(const std::string task, std::string API, UINT64 id, const string type);

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
    static std::string privateGOCQFormat(std::string message, UINT64 user_id, const string type);
    static std::string groupGOCQFormat(std::string message, UINT64 group_id, const string type);

private:
    static std::queue<std::string> *origina_queue; // 原始消息队列
    static std::mutex original_mutex;              // 原始锁

    static std::queue<std::string> *pending_queue; // 待发送消息队列
    static std::mutex pending_mutex;               // 待发送锁
};

#endif // MESSAGETASK_H