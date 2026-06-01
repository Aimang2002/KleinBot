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
    // std::lock_guard(original_mutex);
    original_mutex.lock();
    origina_queue->push(task);
    original_mutex.unlock();
}

void MessageQueue::pending_push_queue(const std::string task, std::string API, uint64_t id, const std::string type)
{
#ifdef DEBUG
    LOG_DEBUG("pending_push_queue的入列消息：" + task);
#endif

    // 数据封装
    std::string JsonFormatData = task; // 用于接收格式化的json数据
    std::string getGOCQJsonData;       // 用于接收装有cq码的Json数据
    std::string webSocketDataPakage;   // 用于接收websocket的数据包格式

    // 判断是群消息还是私聊消息
    if (API.compare(ConfigManager::getInstance().configVariable("GROUP_API")) == 0)
    {
        getGOCQJsonData = groupGOCQFormat(JsonFormatData, id, type);
        webSocketDataPakage = MyReverseWebSocket::messageEncapsulation(getGOCQJsonData, ConfigManager::getInstance().configVariable("GROUP_API"));
    }
    else
    {
        getGOCQJsonData = privateGOCQFormat(JsonFormatData, id, type);
        webSocketDataPakage = MyReverseWebSocket::messageEncapsulation(getGOCQJsonData, ConfigManager::getInstance().configVariable("PRIVATE_API"));
    }

    // 放入消息队列
    pending_mutex.lock();
    pending_queue->push(webSocketDataPakage);
    pending_mutex.unlock();
}

// 判断消息队列是否为空
bool MessageQueue::original_empty()
{
    return origina_queue->empty();
}
bool MessageQueue::pending_empty()
{
    return pending_queue->empty();
}

// 获取第一个消息
std::string MessageQueue::original_front_queue()
{
    std::lock_guard<std::mutex> lock(original_mutex);
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
    // std::lock_guard(pending_mutex);
    pending_mutex.lock();
    result = pending_queue->front();
    pending_mutex.unlock();
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

    // std::lock_guard(original_mutex);
    original_mutex.lock();
    origina_queue->pop();
    original_mutex.unlock();
    return true;
}

bool MessageQueue::pending_pop()
{
    if (pending_queue->empty())
    {
        LOG_WARNING("发送队列为空！或许是程序出了问题，请检查...");
        return false;
    }

    // std::lock_guard(pending_mutex);
    original_mutex.lock();
    pending_queue->pop();
    original_mutex.unlock();
    return true;
}

// 封装GO-CQ格式数据
std::string MessageQueue::privateGOCQFormat(const std::string &message, const uint64_t &user_id, const std::string type)
{
    nlohmann::json json_data;
    json_data["user_id"] = user_id;
    json_data["message"] = message;
    json_data["type"] = type;

    return json_data.dump();
}

std::string MessageQueue::groupGOCQFormat(const std::string &message, const uint64_t &group_id, const std::string type)
{
    nlohmann::json json_data;
    json_data["group_id"] = group_id;
    json_data["message"] = message;
    if (type == "text")
    {
        json_data["type"] = "text";
    }
    return json_data.dump();
}