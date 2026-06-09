#include "MessageQueue.h"
#include "../Network/MyReverseWebSocket.h"
#include "../JsonParse/JsonParse.h"
#include <sstream>

// 静态成员初始化
std::unique_ptr<std::queue<std::string>> MessageQueue::origina_queue = std::make_unique<std::queue<std::string>>();
std::mutex MessageQueue::original_mutex = std::mutex();
std::unique_ptr<std::queue<std::string>> MessageQueue::pending_queue = std::make_unique<std::queue<std::string>>();

std::mutex MessageQueue::pending_mutex = std::mutex();

// 消息入列
void MessageQueue::original_push_queue(std::string task)
{
#ifdef DEBUG
    LOG_DEBUG("original_push_queue的入列消息：" + task);
#endif
    std::lock_guard<std::mutex> locker(original_mutex);
    origina_queue->push(task);
}

void MessageQueue::pending_push_raw(const std::string &packed)
{
#ifdef DEBUG
    LOG_DEBUG("pending_push_raw 入列：" + packed);
#endif
    std::lock_guard<std::mutex> locker(pending_mutex);
    pending_queue->push(packed);
}

// 判断消息队列是否为空
bool MessageQueue::original_empty()
{
    std::lock_guard<std::mutex> locker(original_mutex);
    return origina_queue->empty();
}
bool MessageQueue::pending_empty()
{
    std::lock_guard<std::mutex> locker(pending_mutex);
    return pending_queue->empty();
}

// 获取第一个消息
std::string MessageQueue::original_front_queue()
{
    std::lock_guard<std::mutex> locker(original_mutex);
    if (origina_queue->empty())
    {
        return "当前task为空!请判断队列是否存在数据再获取...";
    }
    std::string result;
    result = origina_queue->front();
    return result;
}

std::string MessageQueue::pending_front_queue()
{
    if (pending_queue->empty())
    {
        return "当前task为空!请判断队列是否存在数据再获取...";
    }

    std::string result;
    std::lock_guard<std::mutex> locker(pending_mutex);
    result = pending_queue->front();
    return result;
}

// 弹出第一个消息
bool MessageQueue::original_pop()
{
    if (origina_queue->empty())
    {
        LOG_WARNING("发送队列为空！或许是程序出了问题，请检查...");
        return false;
    }

    std::lock_guard<std::mutex> locker(original_mutex);
    origina_queue->pop();
    return true;
}

bool MessageQueue::pending_pop()
{
    if (pending_queue->empty())
    {
        LOG_WARNING("发送队列为空！或许是程序出了问题，请检查...");
        return false;
    }

    std::lock_guard<std::mutex> locker(pending_mutex);
    pending_queue->pop();
    return true;
}