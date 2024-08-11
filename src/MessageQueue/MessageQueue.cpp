#include "MessageQueue.h"

// 静态成员初始化
std::queue<std::string> *MessageQueue::origina_queue = new std::queue<std::string>;
std::mutex MessageQueue::original_mutex = std::mutex();
std::queue<std::string> *MessageQueue::pending_queue = new std::queue<std::string>;
std::mutex MessageQueue::pending_mutex = std::mutex();

// 消息入列
void MessageQueue::original_push_queue(std::string task)
{
#ifdef DEBUG
    cout << "入列消息：" << task << endl;
#endif
    // std::lock_guard(original_mutex);
    original_mutex.lock();
    origina_queue->push(task);
    original_mutex.unlock();
}

void MessageQueue::pending_push_queue(std::string task, std::string API, UINT64 id)
{
#ifdef DEBUG
    cout << "入列消息：" << task << endl;
#endif

    // 数据封装
    std::string JsonFormatData;      // 用于接收格式化的json数据
    std::string getGOCQJsonData;     // 用于接收装有cq码的Json数据
    std::string webSocketDataPakage; // 用于接收websocket的数据包格式

    JsonFormatData = JParsingClass.toJson(task);
    // 判断是群消息还是私聊消息
    if (API.compare(CManager.configVariable("GROUP_API")) == 0)
    {
        getGOCQJsonData = groupGOCQFormat(JsonFormatData, id);
        webSocketDataPakage = MyReverseWebSocket::messageEncapsulation(getGOCQJsonData, CManager.configVariable("GROUP_API"));
    }
    else
    {
        getGOCQJsonData = privateGOCQFormat(JsonFormatData, id);
        webSocketDataPakage = MyReverseWebSocket::messageEncapsulation(getGOCQJsonData, CManager.configVariable("PRIVATE_API"));
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
    if (origina_queue->empty())
    {
        return "当前task为空!请判断队列是否存在数据再获取...";
    }

    std::string result;
    // std::lock_guard(original_mutex);
    original_mutex.lock();
    result = origina_queue->front();
    original_mutex.unlock();
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
std::string MessageQueue::privateGOCQFormat(std::string message, UINT64 user_id)
{
    stringstream post_json;
    post_json << R"({"user_id":)" << user_id << R"(,"message":")" << message << R"("})";
    return post_json.str();
}

std::string MessageQueue::groupGOCQFormat(std::string message, UINT64 group_id)
{
    stringstream json_data;
    json_data << R"({"group_id":)" << group_id << R"(,"message":")" << message << R"("})";
    return json_data.str();
}