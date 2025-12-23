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

#define __KLEIN_VERSION__ "v2.4.0"

// 子线程
void pollingThread()
{
	std::string message;
	std::string content;
	std::string JsonFormatData;
	std::string getGOCQJsonData;
	std::string webSocketDataPakage;
	uint64_t user_id;
	bool tag = false;

	while (true)
	{
		std::time_t now = std::time(nullptr);
		// 转换为本地时间
		std::tm *local_time = std::localtime(&now);
		if (local_time->tm_hour == 8 && local_time->tm_min == 0 && local_time->tm_sec < 4) // 每日定时
		{
			content = "早上好，请跟我打招呼的同时来一句元气满满的句子，让我一整天都有活力（直接说就好，不要在前面加上语气词例如“好的”）";
			user_id = stoi(ConfigManager::getInstance().configVariable("MANAGER_QQ")); // 当前版本仅限于管理员账号，后期可选择对外提供服务
			LOG_INFO("每日早安即将发送，亲爱的管理员，早上好。");
			tag = true;
		}
		else if (!TimingTast::getInstance().Event->empty() && TimingTast::getInstance().Event->begin()->first <= TimingTast::getInstance().getPresentTime())
		{
			content = JsonParse::getInstance().toJson(TimingTast::getInstance().Event->begin()->second.second);
			user_id = TimingTast::getInstance().Event->begin()->second.first;
			TimingTast::getInstance().Event->erase(TimingTast::getInstance().Event->begin()); // 删除定时事件
			tag = true;
		}

		if (tag)
		{
			// 数据处理&发送
			message = "“";
			message += content;
			JsonFormatData = "[\n\t";
			JsonFormatData += R"({"role": "user", "content": ")" + message + "\"}\n]";
			// std::pair<std::string, std::vector<std::string>> p;
			Person p;
			p.frequency_penalty = ConfigManager::getInstance().configVariable("frequency_penalty");
			p.isOpenVoiceMode = false;
			p.presence_penalty = ConfigManager::getInstance().configVariable("presence_penalty");
			p.temperature = ConfigManager::getInstance().configVariable("temperature");
			p.top_p = ConfigManager::getInstance().configVariable("top_p");
			p.user_models.first = ConfigManager::getInstance().configVariable("DEFAULT_MODEL");
			p.user_models.second.push_back(ConfigManager::getInstance().configVariable("DEFAULT_MODEL_API_KEY"));
			p.user_models.second.push_back(ConfigManager::getInstance().configVariable("DEFAULT_MODEL_ENDPOINT"));
			p.user_models.second.push_back(ConfigManager::getInstance().configVariable("DEFAULT_MODEL_APISTANDARD"));

			Dock::RequestGPT(JsonFormatData, &p);
			if (JsonFormatData.size() > 100)
				JsonFormatData = JsonParse::getInstance().getAttributeFromChoices(JsonFormatData, "content");

			MessageQueue::pending_push_queue(JsonFormatData, ConfigManager::getInstance().configVariable("PRIVATE_API"), user_id, "text");
			tag = false;
		}
		std::this_thread::sleep_for(std::chrono::seconds(3));
	}
}

void send_message(Message &messageClass, JsonData &data, bool isErrorTransfer)
{
	// 数据处理 && 封装
	std::string getResponse; // 用于接收相应内容

	if (isErrorTransfer) // 错误消息发送
	{
		if (data.message_type == "group")
		{
			// 如果该消息为非at则不发送
			if (!messageClass.messageFilter(data.message_type, data.raw_message))
			{
				LOG_INFO("群消息且非AT消息触发的错误警告，该消息将会不会被处理和发送...");
				return;
			}
			MessageQueue::pending_push_queue(data.raw_message, ConfigManager::getInstance().configVariable("GROUP_API"), data.group_id, "text");
		}
		else
		{
			MessageQueue::pending_push_queue(data.raw_message, ConfigManager::getInstance().configVariable("PRIVATE_API"), data.user_id, "text");
		}
	}
	else
	{
		if (strcmp(data.message_type.c_str(), "group") == 0)
		{
			getResponse = messageClass.handleMessage(data);
			if (getResponse.size() > 5000 && getResponse.size() <= 15000) // GO-CQHTTP插件的单次发送最大长度为5000，而模型单次回复可能达到4096*3的长度
			{
				// 截取两段
				LOG_WARNING("文本过长，将使用分批次发送");
				while (true)
				{
					std::string subStr = getResponse.substr(0, 5000);
					MessageQueue::pending_push_queue(subStr, ConfigManager::getInstance().configVariable("GROUP_API"), data.group_id, data.type);
					getResponse.erase(0, 5000);
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					if (getResponse.size() < 4999)
					{
						MessageQueue::pending_push_queue(getResponse, ConfigManager::getInstance().configVariable("GROUP_API"), data.group_id, data.type);
						break;
					}
				}
			}
			else
			{
				MessageQueue::pending_push_queue(getResponse, ConfigManager::getInstance().configVariable("GROUP_API"), data.group_id, data.type);
			}
		}
		else
		{
			getResponse = messageClass.handleMessage(data);
			if (getResponse.size() > 4096) // && getResponse.size() <= 15000
			{
				// 字符串完整截取
				auto cut_utf8_front =
					[](std::string &str, size_t max_chars) -> std::string
				{
					size_t end_byte_pos = 0;
					size_t char_count = 0;
					for (size_t i = 0; i < str.length() && char_count < max_chars;)
					{
						int len = 1;
						unsigned char c = str[i];
						if ((c & 0xF8) == 0xF0)
							len = 4;
						else if ((c & 0xF0) == 0xE0)
							len = 3;
						else if ((c & 0xE0) == 0xC0)
							len = 2;

						if (i + len > str.length())
							break;

						i += len;
						end_byte_pos = i;
						char_count++;
					}

					std::string result = str.substr(0, end_byte_pos);
					str.erase(0, end_byte_pos); // 从原字符串中移除截取的部分
					return result;
				};

				LOG_WARNING("文本过长，将使用分批次发送");
				while (true)
				{
					std::string subStr = cut_utf8_front(getResponse, 4096);
					MessageQueue::pending_push_queue(subStr, ConfigManager::getInstance().configVariable("PRIVATE_API"), data.user_id, data.type);
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					if (getResponse.size() < 4999)
					{
						MessageQueue::pending_push_queue(getResponse, ConfigManager::getInstance().configVariable("PRIVATE_API"), data.user_id, data.type);
						break;
					}
				}
			}
			else
			{
				MessageQueue::pending_push_queue(getResponse, ConfigManager::getInstance().configVariable("PRIVATE_API"), data.user_id, data.type);
			}
		}
	}
}

// 子线程
void workingThread(Message &messageClass, std::string originalJsonData)
{
	// 内容提取
	JsonData data = JsonParse::getInstance().jsonReader(originalJsonData);
	// 超出最大限度
	if (originalJsonData.size() > (stoi(ConfigManager::getInstance().configVariable("MODEL_SIGLE_TOKEN_MAX")) * 3)) // UTF-8中，1个汉字占用3字节，这里以OpenAI为标准（OpenAI的分词器是1个汉字1个token），这里选择乘3倍
	{
		data.raw_message = "系统提示：消息长度超过最大限度，请减少单次发送的字符数量...";

		send_message(messageClass, data, true);

		return;
	}

	// 消息过滤  检查消息中是否属于转发消息
	if (!messageClass.messageFilter(data.message_type, data.raw_message))
	{
		return;
	}
	// 正常发送
	send_message(messageClass, data, false);
}

void createTimingTastThread()
{
	// 创建子线程
	std::thread t(pollingThread);
	t.detach(); // 线程分离
	return;
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

	Database::getInstance()->databaseEmpty();
	createTimingTastThread(); // 创建子线程
}

int main()
{
	init();
	Message messageClass;

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
