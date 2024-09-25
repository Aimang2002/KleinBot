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

#define __KLEIN_VERSION__ "v2.3.1"

using namespace std;

ConfigManager &CManager = *new ConfigManager("config.json");
JsonParse &JParsingClass = *new JsonParse;
TimingTast &TTastClass = *new TimingTast;
Message &messageClass = *new Message;

// 子线程
void pollingThread()
{
	string message;
	string content;
	string JsonFormatData;
	string getGOCQJsonData;
	string webSocketDataPakage;
	UINT64 user_id;
	bool tag = false;

	while (true)
	{
		std::time_t now = std::time(nullptr);
		// 转换为本地时间
		std::tm *local_time = std::localtime(&now);
		if (local_time->tm_hour == 8 && local_time->tm_min == 0 && local_time->tm_sec < 4) // 每日定时
		{
			content = "早上好，请跟我打招呼的同时来一句元气满满的句子，让我一整天都有活力（直接说就好，不要在前面加上语气词例如“好的”）";
			user_id = stoi(CManager.configVariable("MANAGER_QQ")); // 当前版本仅限于管理员账号，后期可选择对外提供服务
			LOG_INFO("每日早安即将发送，亲爱的管理员，早上好。");
			tag = true;
		}
		else if (!TTastClass.Event->empty() && TTastClass.Event->begin()->first <= TTastClass.getPresentTime())
		{
			content = JParsingClass.toJson(TTastClass.Event->begin()->second.second);
			user_id = TTastClass.Event->begin()->second.first;
			TTastClass.Event->erase(TTastClass.Event->begin()); // 删除定时事件
			tag = true;
		}

		if (tag)
		{
			// 数据处理&发送
			message = "“";
			message += content;
			JsonFormatData = "[\n\t";
			JsonFormatData += R"({"role": "user", "content": ")" + message + "\"}\n]";
			pair<string, string> p;
			p.first = CManager.configVariable("DEFAULT_MODEL");
			p.second = CManager.configVariable("DEFAULT_MODEL_SERVICE");
			Dock::RequestGPT(JsonFormatData, p);
			if (JsonFormatData.size() > 100)
			{
				JsonFormatData = JParsingClass.getAttributeFromChoices(JsonFormatData, "content");
			}
			MessageQueue::pending_push_queue(JsonFormatData, CManager.configVariable("PRIVATE_API"), user_id);
			tag = false;
		}
		sleep(3);
	}
}

void send_message(JsonData &data, bool isErrorTransfer)
{
	// 数据处理 && 封装
	string getResponse; // 用于接收相应内容

	if (isErrorTransfer)
	{
		if (data.message_type == "group")
		{
			MessageQueue::pending_push_queue(data.message, CManager.configVariable("GROUP_API"), data.group_id);
		}
		else
		{
			MessageQueue::pending_push_queue(data.message, CManager.configVariable("PRIVATE_API"), data.private_id);
		}
	}
	else
	{
		if (strcmp(data.message_type.c_str(), "group") == 0)
		{
			getResponse = messageClass.handleMessage(data.private_id, data.message, data.message_type);
			if (getResponse.size() > 5000 && getResponse.size() <= 15000) // GO-CQHTTP插件的单次发送最大长度为5000，而模型单次回复可能达到4096*3的长度
			{
				// 截取两段
				LOG_WARNING("文本过长，将使用分批次发送");
				while (true)
				{
					string subStr = getResponse.substr(0, 5000);
					MessageQueue::pending_push_queue(subStr, CManager.configVariable("GROUP_API"), data.group_id);
					getResponse.erase(0, 5000);
					sleep(1);
					if (getResponse.size() < 4999)
					{
						MessageQueue::pending_push_queue(getResponse, CManager.configVariable("GROUP_API"), data.group_id);
						break;
					}
				}
			}
			else
			{
				MessageQueue::pending_push_queue(getResponse, CManager.configVariable("GROUP_API"), data.group_id);
			}
		}
		else
		{
			getResponse = messageClass.handleMessage(data.private_id, data.message, data.message_type);
			if (getResponse.size() > 5000 > 5000 && getResponse.size() <= 15000)
			{
				LOG_WARNING("文本过长，将使用分批次发送");
				while (true)
				{
					string subStr = getResponse.substr(0, 5000);
					MessageQueue::pending_push_queue(subStr, CManager.configVariable("PRIVATE_API"), data.private_id);
					getResponse.erase(0, 5000);
					sleep(1);
					if (getResponse.size() < 4999)
					{
						MessageQueue::pending_push_queue(getResponse, CManager.configVariable("PRIVATE_API"), data.private_id);
						break;
					}
				}
			}
			else
			{
				MessageQueue::pending_push_queue(getResponse, CManager.configVariable("PRIVATE_API"), data.private_id);
			}
		}
	}
}

// 子线程
void workingThread(string originalJsonData)
{
	// 内容提取
	JsonData *data = new JsonData(JParsingClass.jsonReader(originalJsonData));

	// 超出最大限度
	if (originalJsonData.size() > (stoi(CManager.configVariable("MODEL_SIGLE_TOKEN_MAX")) * 3)) // UTF-8中，1个汉字占用3字节，这里以OpenAI为标准（OpenAI的分词器是1个汉字1个token），这里选择乘3倍
	{
		data->message = "系统提示：消息长度超过最大限度，请减少单次发送的字符数量...";
		send_message(*data, true);

		delete data;
		return;
	}

	// 消息过滤
	if (!messageClass.messageFilter(data->message_type, data->message))
	{
		return;
	}

	// 正常发送
	send_message(*data, false);

	delete data;
}

void createTimingTastThread()
{
	// 创建子线程
	thread t(pollingThread);
	t.detach(); // 线程分离
	return;
}

// 资源释放
void resourceCleanup()
{
	if (&CManager != nullptr)
	{
		delete &CManager;
	}

	if (&JParsingClass != nullptr)
	{
		delete &JParsingClass;
	}

	if (&TTastClass != nullptr)
	{
		delete &TTastClass;
	}

	if (&messageClass != nullptr)
	{
		delete &messageClass;
	}
}

void init()
{

	// LOGO
	string Klein_logo =
		R"( -------------------------------------------
| ██╗  ██╗ ██╗      ███████╗ ██╗ ███╗   ██╗ |
| ██║ ██╔╝ ██║      ██╔════╝ ██║ ████╗  ██║ |
| █████╔╝  ██║      █████╗   ██║ ██╔██╗ ██║ |
| ██╔═██╗  ██║      ██╔══╝   ██║ ██║╚██╗██║ |
| ██║  ██╗ ███████╗ ███████╗ ██║ ██║ ╚████║ |
| ╚═╝  ╚═╝ ╚══════╝ ╚══════╝ ╚═╝ ╚═╝  ╚═══╝ |
 -------------------------------------------)";

	cout << "\033[32m" << "\n"
		 << Klein_logo << "\n"
		 << "\033[0m" << endl; // 显示logo

	// 版本
	LOG_INFO("当前Klein版本：" + string(__KLEIN_VERSION__));
	LOG_INFO("当前配置文件版本：" + CManager.configVariable("CONFIG_VERSION"));

	// 配置文件版本检查
	if (CManager.configVariable("CONFIG_VERSION") == __KLEIN_VERSION__)
	{
		LOG_INFO("配置文件符合版本。");
	}
	else
	{
		LOG_WARNING("配置文件不符合当前版本，程序可能会不稳定，建议使用适合版本的配置文件！");
	}

	Database::getInstance()->databaseEmpty();
	createTimingTastThread(); // 创建子线程
}

int main()
{
	init();

	// 正向WebSocket连接
	std::thread t1(MyWebSocket::connectWebSocket, "/");
	t1.detach();

	sleep(1);
	LOG_INFO("3秒后连接反向WebSocket...");
	sleep(3);

	// 反向WebSocket连接
	std::thread t2(MyReverseWebSocket::connectReverseWebSocket);
	t2.detach();

	// 轮询originalMessageQueue
	while (true)
	{
		if (!MessageQueue::original_empty())
		{
			std::thread t(workingThread, MessageQueue::original_front_queue());
			t.detach();
			MessageQueue::original_pop();
		}
		else
		{
			// 休眠算法...
			sleep(1);
		}
	}

	// 判断是否退出 ...

	// 资源回收
	resourceCleanup();

	return 0;
}