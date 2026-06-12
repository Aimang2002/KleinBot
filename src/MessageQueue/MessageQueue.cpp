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

// 原子出队：空判断+取首+弹出在同一把锁内完成
std::optional<std::string> MessageQueue::original_try_pop()
{
    std::lock_guard<std::mutex> locker(original_mutex);
    if (origina_queue->empty())
    {
        return std::nullopt;
    }
    std::string result = std::move(origina_queue->front());
    origina_queue->pop();
    return result;
}

std::optional<std::string> MessageQueue::pending_try_pop()
{
    std::lock_guard<std::mutex> locker(pending_mutex);
    if (pending_queue->empty())
    {
        return std::nullopt;
    }
    std::string result = std::move(pending_queue->front());
    pending_queue->pop();
    return result;
}