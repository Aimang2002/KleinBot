#include <iostream>
#include <sstream>
#include <random>
#include <thread>
#include "JsonParse/JsonParse.h"
#include "ConfigManager/ConfigManager.h"
#include "Network/Network.h"
#include "Message/Message.h"
#include "Log/Log.h"
#include "MessageQueue.h"
#include "src/Network/MyReverseWebSocket.h"
#include "src/Network/MyWebSocket.h"
#include "TimingTast/TimingTast.h"
#include "MessageSender/QQMessageSender.h"
#include "Port/OutboundMessage.h"
#include "ChatService/ChatService.h"

#define __KLEIN_VERSION__ "v2.4.0"

// 子线程
void pollingThread(Message &msg, MessageSenderPort &sender)
{
	std::string message;
	uint64_t user_id;
	bool tag = false;

	while (true)
	{
		std::time_t now = std::time(nullptr);
		std::tm *local_time = std::localtime(&now);
		if (local_time->tm_hour == 8 && local_time->tm_min == 0 && local_time->tm_sec < 4)
		{
			message = "早上好，请跟我打招呼的同时来一句元气满满的句子，让我一整天都有活力（直接说就好，不要在前面加上语气词例如“好的”）";
			user_id = stoi(ConfigManager::getInstance().configVariable("MANAGER_QQ"));
			LOG_INFO("每日早安即将发送，亲爱的管理员，早上好。");
			tag = true;
		}
		else if (!TimingTast::getInstance().Event->empty() && TimingTast::getInstance().Event->begin()->first <= TimingTast::getInstance().getPresentTime())
		{
			message = JsonParse::getInstance().toJson(TimingTast::getInstance().Event->begin()->second.second);
			user_id = TimingTast::getInstance().Event->begin()->second.first;
			TimingTast::getInstance().Event->erase(TimingTast::getInstance().Event->begin());
			tag = true;
		}

		if (tag)
		{
			std::string response = msg.pushScheduled(message);
			sender.send_private(static_cast<long long>(user_id), TextMessage{response});
			tag = false;
		}
		std::this_thread::sleep_for(std::chrono::seconds(3));
	}
}

// 子线程
void workingThread(Message &messageClass, std::string originalJsonData)
{
	JsonData data = JsonParse::getInstance().jsonReader(originalJsonData);

	// UTF-8 下 1 汉字 ≈ 3 字节；OpenAI 分词器以汉字数计 token，这里按 3 倍粗略放宽上限
	if (originalJsonData.size() > (stoi(ConfigManager::getInstance().configVariable("MODEL_SIGLE_TOKEN_MAX")) * 3))
	{
		if (data.message_type == "group" && !messageClass.messageFilter(data.message_type, data.raw_message))
		{
			LOG_INFO("群消息且非AT消息触发的错误警告，该消息将会不会被处理和发送...");
			return;
		}
		messageClass.sendError(data, "系统提示：消息长度超过最大限度，请减少单次发送的字符数量...");
		return;
	}

	if (!messageClass.messageFilter(data.message_type, data.raw_message))
	{
		return;
	}

	messageClass.handleMessage(data);
}

void createTimingTastThread(Message &msg, MessageSenderPort &sender)
{
	std::thread t(pollingThread, std::ref(msg), std::ref(sender));
	t.detach();
}

// 资源释放
void resourceCleanup()
{
}

void init()
{

#if defined(__WIN32) || defined(__WIN64)
	std::system("chcp 65001");
#endif

	// LOGO
	std::string Klein_logo = R"( -------------------------------------------
| ██╗  ██╗ ██╗      ███████╗ ██╗ ███╗   ██╗ |
| ██║ ██╔╝ ██║      ██╔════╝ ██║ ████╗  ██║ |
| █████╔╝  ██║      █████╗   ██║ ██╔██╗ ██║ |
| ██╔═██╗  ██║      ██╔══╝   ██║ ██║╚██╗██║ |
| ██║  ██╗ ███████╗ ███████╗ ██║ ██║ ╚████║ |
| ╚═╝  ╚═╝ ╚══════╝ ╚══════╝ ╚═╝ ╚═╝  ╚═══╝ |
 -------------------------------------------)";

	std::cout << "\033[32m" << "\n"
			  << Klein_logo << "\n"
			  << "\033[0m" << std::endl; // 显示logo

	// 版本
	LOG_INFO("当前Klein版本：" + std::string(__KLEIN_VERSION__));
	LOG_INFO("当前配置文件版本：" + ConfigManager::getInstance().configVariable("CONFIG_VERSION"));

	// 配置文件版本检查
	if (ConfigManager::getInstance().configVariable("CONFIG_VERSION") == __KLEIN_VERSION__)
	{
		LOG_INFO("配置文件符合版本。");
	}
	else
	{
		LOG_WARNING("配置文件不符合当前版本，程序可能会不稳定，建议使用适合版本的配置文件！");
	}
}

int main()
{
	init();
	QQMessageSender qqSender;
	Message messageClass(qqSender);
	createTimingTastThread(messageClass, qqSender);

	// 正向WebSocket连接
	std::thread t1(MyWebSocket::connectWebSocket, "/");
	t1.detach();

	std::this_thread::sleep_for(std::chrono::seconds(1));
	LOG_INFO("3秒后连接反向WebSocket...");
	std::this_thread::sleep_for(std::chrono::seconds(3));

	// 反向WebSocket连接
	std::thread t2(MyReverseWebSocket::connectReverseWebSocket);
	t2.detach();

	// 轮询originalMessageQueue  这里可以使用线程池管理
	while (true)
	{
		if (!MessageQueue::original_empty())
		{
			std::thread t(workingThread, std::ref(messageClass), MessageQueue::original_front_queue());
			t.detach();
			MessageQueue::original_pop();
		}
		else
		{
			// 休眠算法...
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	}

	// 判断是否退出 ...

	// 资源回收
	resourceCleanup();

	return 0;
}
