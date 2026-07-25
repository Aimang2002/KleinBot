#include <iostream>
#include <sstream>
#include <random>
#include <thread>
#include <atomic>
#include <csignal>
#include "JsonParse/JsonParse.h"
#include "Bootstrap/RuntimeSettings.h"
#include "Configuration/ConfigLoader.h"
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
#include "Command/AdminCommand.h"
#include "Command/GeneratePictureCommand.h"
#include "Command/HelpCommand.h"
#include "Command/ModelListCommand.h"
#include "Command/QueryModelCommand.h"
#include "Command/RemoveContextCommand.h"
#include "Command/ResetChatCommand.h"
#include "Command/SearchSongsCommand.h"
#include "Command/SetSoulCommand.h"
#include "Command/SwitchModelCommand.h"
#include "Command/VoiceSwitchCommand.h"
#include "Tool/RecallConversationTool.h"
#include "Tool/GenerateImageTool.h"
#include "Tool/InspectImageTool.h"
#include "Tool/SendImageTool.h"
#include "ModelApiCaller/Voice/Voice.h"
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


void logConfigDiagnostics(const ConfigLoadResult &result)
{
	for (const ConfigDiagnostic &diagnostic : result.diagnostics)
	{
		const std::string message = configSeverityName(diagnostic.severity) + ": " +
			diagnostic.path + " - " + diagnostic.message;
		if (diagnostic.severity == ConfigSeverity::Fatal || diagnostic.severity == ConfigSeverity::Error)
			LOG_ERROR(message);
		else if (diagnostic.severity == ConfigSeverity::Warning || diagnostic.severity == ConfigSeverity::FeatureDisabled)
			LOG_WARNING(message);
		else
			LOG_INFO(message);
	}
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

}

// 子线程
void pollingThread(ChatService &chatService, MessageSenderPort &sender, uint64_t managerId, const std::atomic<bool> &running)
{
	std::string message;
	uint64_t user_id;
	bool tag = false;

	while (running.load())
	{
		std::time_t now = std::time(nullptr);
		std::tm *local_time = std::localtime(&now);
		if (managerId != 0 && local_time->tm_hour == 8 && local_time->tm_min == 0 && local_time->tm_sec < 4)
		{
			message = "早上好，请跟我打招呼的同时来一句元气满满的句子，让我一整天都有活力（直接说就好，不要在前面加上语气词例如“好的”）";
			user_id = managerId;
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
void workingThread(Message &messageClass, InboundMessage data, std::size_t maxMessageTokens)
{
	// UTF-8 下 1 汉字 ≈ 3 字节；OpenAI 分词器以汉字数计 token，这里按 3 倍粗略放宽上限
	if (data.payload_size_bytes >
		(maxMessageTokens * 3))
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

void init(int schemaVersion)
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

	LOG_INFO("当前Klein版本：" + std::string(__KLEIN_VERSION__));
	LOG_INFO("当前配置Schema版本：" + std::to_string(schemaVersion));
}

int main(int argc, char **argv)
{
	std::signal(SIGINT, signalHandler);
	std::signal(SIGTERM, signalHandler);
	std::atomic<bool> running{true};

	std::string configPath = "config.json";
	bool checkConfigOnly = false;
	for (int index = 1; index < argc; ++index)
	{
		const std::string argument = argv[index];
		if (argument == "--check-config")
			checkConfigOnly = true;
		else if (argument == "--config" && index + 1 < argc)
			configPath = argv[++index];
		else
		{
			std::cerr << "未知参数或缺少参数值：" << argument << std::endl;
			return -1;
		}
	}

	ConfigLoader configLoader;
	ConfigLoadResult loadResult = configLoader.loadFile(configPath);
	logConfigDiagnostics(loadResult);
	if (!loadResult.canStart())
	{
		LOG_FATAL("配置加载失败，程序无法安全启动");
		Log::getInstance().shutdown();
		return -1;
	}
	const std::shared_ptr<const SchemaConfig> schema = loadResult.config;
	if (checkConfigOnly)
	{
		LOG_INFO("配置校验通过：" + configPath);
		Log::getInstance().shutdown();
		return 0;
	}
	const RuntimeSettings settings = buildRuntimeSettings(*schema);
	init(settings.schemaVersion);
	const TransportConfig &transportConfig = settings.transport;
	LOG_INFO("当前通信模式：" + transportModeName(transportConfig.mode));
	ModelRegistry models(settings.models.registryPath);
	const std::string &dbPath = settings.storage.conversationDatabase;
	ConversationStore conversationStore(dbPath);
	ImageAssetStore imageAssetStore(dbPath, settings.storage.imageAssets);
	UserSessionService userSession(models, conversationStore, settings.bot, settings.chat);
	Dock dock(settings.dock);
	MemoryService memoryService(dbPath, conversationStore, dock, models, settings.memory);
	userSession.setMemoryService(&memoryService);
	userSession.setImageAssetStore(&imageAssetStore);
	if (settings.bot.managerId != 0)
		userSession.ensureUserExists(settings.bot.managerId);
	ToolRegistry tools;
	bool globalVoice = settings.voice.enabled;
	bool accessibilityChat = schema->accessibilityChat;
	ComputerStatus adminComputerStatus;
	GetCurrentModelAction getCurrentModelAction([&userSession](uint64_t userId)
		{ return userSession.getModelName(userId); });
	VoiceModeAction voiceModeAction(userSession, globalVoice);
	AdminControlAction adminControlAction(adminComputerStatus, accessibilityChat,
		globalVoice, [&models]()
		{
			models.reload();
		});
	tools.registerTool(std::make_unique<GetTimeTool>());
	tools.registerTool(std::make_unique<ActionTool>(getCurrentModelAction));
	tools.registerTool(std::make_unique<RecallConversationTool>(memoryService));
	tools.registerTool(std::make_unique<GenerateImageTool>(dock, imageAssetStore, settings.models.drawing));
	tools.registerTool(std::make_unique<InspectImageTool>(dock, imageAssetStore, settings.models.vision));
	tools.registerTool(std::make_unique<SendImageTool>(imageAssetStore));
	tools.registerTool(std::make_unique<ActionTool>(voiceModeAction));
	tools.registerTool(std::make_unique<ActionTool>(adminControlAction));
	ChatService chatService(dock, userSession, models, tools, memoryService, settings.chat, settings.bot.managerId);
	InboundMessageQueue inboundQueue;
	OutboundMessageQueue outboundQueue;
	QueuedMessageSender messageSender(outboundQueue);
	OneBotEventDecoder oneBotEventDecoder;
	OneBotMessageEncoder oneBotMessageEncoder(
		settings.chat.privateAction, settings.chat.groupAction);
	Voice voice(settings.voice);
	CommandRegistry commandRegistry(settings.bot.managerId);
	commandRegistry.registryCommand(std::make_unique<HelpCommand>(settings.resources.helpFile));
	commandRegistry.registryCommand(std::make_unique<ModelListCommand>(models));
	commandRegistry.registryCommand(std::make_unique<SearchSongsCommand>());
	commandRegistry.registryCommand(std::make_unique<QueryModelCommand>(getCurrentModelAction));
	commandRegistry.registryCommand(std::make_unique<GeneratePictureCommand>(dock, settings.models.drawing));
	commandRegistry.registryCommand(std::make_unique<ResetChatCommand>(userSession));
	commandRegistry.registryCommand(std::make_unique<SetSoulCommand>(userSession));
	commandRegistry.registryCommand(std::make_unique<SwitchModelCommand>(userSession, models));
	commandRegistry.registryCommand(std::make_unique<VoiceSwitchCommand>(voiceModeAction));
	commandRegistry.registryCommand(std::make_unique<RemoveContextCommand>(userSession));
	commandRegistry.registryCommand(std::make_unique<AdminCommand>(adminControlAction));
	Message messageClass(dock, userSession, chatService, messageSender, imageAssetStore,
		commandRegistry, voice, settings.message, settings.models.vision,
		globalVoice, accessibilityChat);
	ThreadPool messageWorkers(settings.chat.workerThreads);
	std::thread timingThread(pollingThread, std::ref(chatService), std::ref(messageSender), settings.bot.managerId, std::cref(running));

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
			messageWorkers.submit([&messageClass, maxMessageTokens = settings.chat.maxMessageTokens,
				message = std::move(*msg)]() mutable {
				try
				{
					workingThread(messageClass, std::move(message), maxMessageTokens);
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
