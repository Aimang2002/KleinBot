#include <iostream>
#include <sstream>
#include <random>
#include <thread>
#include <atomic>
#include <csignal>
#include "JsonParse/JsonParse.h"
#include "ConfigManager/ConfigManager.h"
#include "Message/Message.h"
#include "Log/Log.h"
#include "MessageQueue/InboundMessageQueue.h"
#include "src/Network/MyReverseWebSocket.h"
#include "src/Network/MyWebSocket.h"
#include "Network/OneBotHttpTransport.h"
#include "Network/TransportConfig.h"
#include "TimingTast/TimingTast.h"
#include "MessageSender/QueuedMessageSender.h"
#include "MessageQueue/OutboundMessageQueue.h"
#include "Port/OutboundMessage.h"
#include "Protocol/OneBot/OneBotMessageEncoder.h"
#include "Protocol/OneBot/OneBotEventDecoder.h"
#include "ChatService/ChatService.h"
#include "Tool/ToolRegistry.h"
#include "Tool/GetTimeTool.h"
#include "Tool/ActionTool.h"
#include "Action/GetCurrentModelAction.h"
#include "Action/VoiceModeAction.h"
#include "Action/AdminControlAction.h"
#include "Tool/RecallConversationTool.h"
#include "Tool/GenerateImageTool.h"
#include "Tool/InspectImageTool.h"
#include "Tool/SendImageTool.h"
#include "Persistence/ConversationStore.h"
#include "Asset/ImageAssetStore.h"
#include "Memory/MemoryService.h"
#include "utils/ThreadPool.h"

#define __KLEIN_VERSION__ "v2.4.0"

namespace
{
volatile std::sig_atomic_t receivedSignal = 0;

void signalHandler(int signal)
{
	receivedSignal = signal;
}

bool sleepWhileRunning(const std::atomic<bool> &running, std::chrono::milliseconds duration)
{
	const auto deadline = std::chrono::steady_clock::now() + duration;
	while (running.load() && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	return running.load();
}

std::size_t messageWorkerCount()
{
	std::string configured = ConfigManager::getInstance().configVariableOpt("MESSAGE_WORKER_THREADS");
	if (!configured.empty())
	{
		try
		{
			const int value = std::stoi(configured);
			if (value > 0)
			{
				return static_cast<std::size_t>(value);
			}
		}
		catch (const std::exception &)
		{
			LOG_WARNING("MESSAGE_WORKER_THREADS 配置无效，将使用默认值");
		}
	}

	return 4;
}
}

// 子线程
void pollingThread(ChatService &chatService, MessageSenderPort &sender, const std::atomic<bool> &running)
{
	std::string message;
	uint64_t user_id;
	bool tag = false;

	while (running.load())
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
		else if (auto due = TimingTast::getInstance().popDueEvent(TimingTast::getInstance().getPresentTime()))
		{
			message = JsonParse::getInstance().toJson(due->content);
			user_id = due->user_id;
			tag = true;
		}

		if (tag)
		{
			std::string response = chatService.replyOneShot(message);
			sender.send_private(static_cast<long long>(user_id), TextMessage{response});
			tag = false;
		}
		sleepWhileRunning(running, std::chrono::seconds(3));
	}
	LOG_INFO("定时任务线程已退出");
}

// 子线程
void workingThread(Message &messageClass, InboundMessage data)
{
	// UTF-8 下 1 汉字 ≈ 3 字节；OpenAI 分词器以汉字数计 token，这里按 3 倍粗略放宽上限
	if (data.payload_size_bytes >
		(static_cast<std::size_t>(stoi(ConfigManager::getInstance().configVariable("MODEL_SIGLE_TOKEN_MAX"))) * 3))
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

void resourceCleanup(std::atomic<bool> &running, ThreadPool &messageWorkers,
					 MemoryService &memoryService,
					 std::thread &timingThread, std::thread &transportThread)
{
	running.store(false);
	LOG_INFO("正在等待消息线程池排空...");
	messageWorkers.shutdown();
	LOG_INFO("消息线程池已退出");
	LOG_INFO("正在等待长期记忆服务退出...");
	memoryService.shutdown();
	LOG_INFO("长期记忆服务已退出");

	if (timingThread.joinable())
	{
		LOG_INFO("正在等待定时任务线程退出...");
		timingThread.join();
	}
	if (transportThread.joinable())
	{
		LOG_INFO("正在等待通信线程退出...");
		transportThread.join();
	}
	LOG_INFO("资源回收完成，Klein 已安全退出");
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
	std::signal(SIGINT, signalHandler);
	std::signal(SIGTERM, signalHandler);
	std::atomic<bool> running{true};

	init();
	TransportConfig transportConfig;
	try
	{
		transportConfig = TransportConfig::fromConfigManager();
	}
	catch (const std::exception &error)
	{
		LOG_FATAL("通信配置无效：" + std::string(error.what()));
		Log::getInstance().shutdown();
		return -1;
	}
	LOG_INFO("当前通信模式：" + transportModeName(transportConfig.mode));
	ModelRegistry models;
	std::string dbPath = ConfigManager::getInstance().configVariableOpt(
		"CONVERSATION_DB_PATH", "source/conversations.db");
	ConversationStore conversationStore(dbPath);
	std::string imageAssetPath = ConfigManager::getInstance().configVariableOpt(
		"IMAGE_ASSET_PATH", "source/image_assets");
	ImageAssetStore imageAssetStore(dbPath, imageAssetPath);
	UserSessionService userSession(models, conversationStore);
	Dock dock;
	MemoryService memoryService(dbPath, conversationStore, dock, models);
	userSession.setMemoryService(&memoryService);
	userSession.setImageAssetStore(&imageAssetStore);
	ToolRegistry tools;
	bool globalVoice = ConfigManager::getInstance().configVariable("GLOBAL_VOICE") == "true";
	bool accessibilityChat = ConfigManager::getInstance().configVariable("ACCESSIBLITY_CHAT") == "true";
	ComputerStatus adminComputerStatus;
	GetCurrentModelAction getCurrentModelAction([&userSession](uint64_t userId)
		{ return userSession.getModelName(userId); });
	VoiceModeAction voiceModeAction(userSession, globalVoice);
	AdminControlAction adminControlAction(adminComputerStatus, accessibilityChat,
		globalVoice, [&models]()
		{
			ConfigManager::getInstance().refreshConfiguation();
			models.reload();
		});
	tools.registerTool(std::make_unique<GetTimeTool>());
	tools.registerTool(std::make_unique<ActionTool>(getCurrentModelAction));
	tools.registerTool(std::make_unique<RecallConversationTool>(memoryService));
	tools.registerTool(std::make_unique<GenerateImageTool>(dock, imageAssetStore));
	tools.registerTool(std::make_unique<InspectImageTool>(dock, imageAssetStore));
	tools.registerTool(std::make_unique<SendImageTool>(imageAssetStore));
	tools.registerTool(std::make_unique<ActionTool>(voiceModeAction));
	tools.registerTool(std::make_unique<ActionTool>(adminControlAction));
	ChatService chatService(dock, userSession, models, tools, memoryService);
	InboundMessageQueue inboundQueue;
	OutboundMessageQueue outboundQueue;
	QueuedMessageSender messageSender(outboundQueue);
	OneBotEventDecoder oneBotEventDecoder;
	OneBotMessageEncoder oneBotMessageEncoder(
		ConfigManager::getInstance().configVariable("PRIVATE_API"),
		ConfigManager::getInstance().configVariable("GROUP_API"));
	Message messageClass(models, dock, userSession, chatService, messageSender, imageAssetStore,
		globalVoice, accessibilityChat, getCurrentModelAction, voiceModeAction,
		adminControlAction);
	ThreadPool messageWorkers(messageWorkerCount());
	std::thread timingThread(pollingThread, std::ref(chatService), std::ref(messageSender), std::cref(running));

	std::thread transportThread;
	switch (transportConfig.mode)
	{
	case TransportMode::ForwardWebSocket:
		transportThread = std::thread(
			MyWebSocket::connectWebSocket,
			std::cref(transportConfig.forwardWebSocket),
			std::ref(inboundQueue), std::ref(outboundQueue),
			std::cref(oneBotEventDecoder), std::cref(oneBotMessageEncoder),
			std::cref(running));
		break;
	case TransportMode::ReverseWebSocket:
		transportThread = std::thread(
			MyReverseWebSocket::connectReverseWebSocket,
			std::cref(transportConfig.reverseWebSocket),
			std::ref(inboundQueue), std::ref(outboundQueue),
			std::cref(oneBotEventDecoder), std::cref(oneBotMessageEncoder),
			std::cref(running));
		break;
	case TransportMode::Http:
		transportThread = std::thread(
			OneBotHttpTransport::run,
			std::cref(transportConfig),
			std::ref(inboundQueue), std::ref(outboundQueue),
			std::cref(oneBotEventDecoder), std::cref(oneBotMessageEncoder),
			std::cref(running));
		break;
	}

	while (running.load())
	{
		if (receivedSignal != 0)
		{
			LOG_INFO("收到退出信号：" + std::to_string(receivedSignal));
			running.store(false);
			break;
		}

		if (auto msg = inboundQueue.tryPop())
		{
			messageWorkers.submit([&messageClass, message = std::move(*msg)]() mutable {
				try
				{
					workingThread(messageClass, std::move(message));
				}
				catch (const std::exception &e)
				{
					LOG_ERROR("消息处理任务异常：" + std::string(e.what()));
				}
			});
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}

	resourceCleanup(running, messageWorkers, memoryService, timingThread,
					transportThread);
	Log::getInstance().shutdown();

	return 0;
}
