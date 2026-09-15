#include <iostream>
#include <sstream>
#include <random>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <csignal>
#include "JsonParse/JsonParse.h"
#include "Application/EventRouter.h"
#include "Bootstrap/ConfigSnapshotStore.h"
#include "Bootstrap/RuntimeSettings.h"
#include "Configuration/ConfigLoader.h"
#include "Configuration/ConfigTemplate.h"
#include "Message/Message.h"
#include "Log/Log.h"
#include "MessageQueue/InboundMessageQueue.h"
#include "src/Network/MyReverseWebSocket.h"
#include "src/Network/MyWebSocket.h"
#include "Network/OneBotHttpTransport.h"
#include "Network/HttpApiChannel.h"
#include "Network/TransportConfig.h"
#include "Network/WebSocketApiChannel.h"
#include "Application/CapabilityBroker.h"
#include "Application/TypingIndicator.h"
#include "Event/FriendRequestNotifier.h"
#include "WebUI/ConfigPanelServer.h"
#include "Persistence/ReminderStore.h"
#include "Reminder/ReminderService.h"
#include "Perception/PerceptionChannel.h"
#include "Perception/PerceptionStore.h"
#include "Perception/GroupContextStore.h"
#include "Perception/GroupContextService.h"
#include "Perception/GroupListService.h"
#include "MessageSender/QueuedMessageSender.h"
#include "MessageQueue/OutboundMessageQueue.h"
#include "Port/OutboundMessage.h"
#include "Version/VersionInfo.h"
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
#include "Command/ResetContextCommand.h"
#include "Command/SearchSongsCommand.h"
#include "Command/SetSoulCommand.h"
#include "Command/SwitchModelCommand.h"
#include "Command/VoiceSwitchCommand.h"
#include "Tool/RecallConversationTool.h"
#include "Action/WebSearchAction.h"
#include "Action/WebFetchAction.h"
#include "Action/SetReminderAction.h"
#include "Action/ListRemindersAction.h"
#include "Action/CancelReminderAction.h"
#include "WebSearch/TavilySearchProvider.h"
#include "Tool/GenerateImageTool.h"
#include "Tool/InspectImageTool.h"
#include "Tool/SendImageTool.h"
#include "ModelApiCaller/Voice/Voice.h"
#include "Persistence/ConversationStore.h"
#include "Asset/ImageAssetStore.h"
#include "Memory/MemoryService.h"
#include "utils/KeyedTaskScheduler.h"
#include "KleinVersion.h"

namespace
{
volatile std::sig_atomic_t receivedSignal = 0;

void signalHandler(int signal)
{
	if (receivedSignal != 0)
		std::_Exit(128 + signal);
	receivedSignal = signal;
}


void logConfigDiagnostics(const std::vector<ConfigDiagnostic> &diagnostics)
{
	for (const ConfigDiagnostic &diagnostic : diagnostics)
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

std::string summarizeReload(const ConfigReloadResult &result)
{
	if (!result.success)
		return "配置刷新失败，继续使用当前内存快照。";
	if (result.diff.empty())
		return "配置校验通过，内存快照无变化。";

	return "配置内存快照已更新：" + std::to_string(result.diff.size()) +
		" 项变化（动态 " +
		std::to_string(result.diff.count(ConfigChangeImpact::Dynamic)) +
		"、需重建 " +
		std::to_string(result.diff.count(ConfigChangeImpact::Rebuild)) +
		"、需重启 " +
		std::to_string(result.diff.count(ConfigChangeImpact::Restart)) +
		"）。当前运行模块尚未重新应用这些参数。";
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
void pollingThread(ChatService &chatService, MessageSenderPort &sender,
				   ReminderService &reminders, CapabilityBroker &capabilities,
				   PerceptionChannel &perception, GroupContextStore *groupContext,
				   GroupListService *groupList,
				   GroupContextService &groupContextService,
				   uint64_t managerId, const std::atomic<bool> &running)
{
	while (running.load())
	{
		std::time_t now = std::time(nullptr);
		// 能力探测：未就绪时尝试（失败 60s 冷却），就绪后此处是无害的快速返回
		capabilities.poll(static_cast<std::int64_t>(now));
		std::tm *local_time = std::localtime(&now);
		if (managerId != 0 && local_time->tm_hour == 8 && local_time->tm_min == 0 && local_time->tm_sec < 4)
		{
			const std::string message = "早上好，请跟我打招呼的同时来一句元气满满的句子，让我一整天都有活力（直接说就好，不要在前面加上语气词例如“好的”）";
			LOG_INFO("每日早安即将发送，亲爱的管理员，早上好。");
			std::string response = chatService.replyInCharacter(managerId, message);
			// 生成失败静默跳过：不发空消息，也不把错误文案发给管理员，下一天再试
			if (!response.empty())
			{
				sender.deliver(OutboundDelivery{
					DirectMessageTarget{std::to_string(managerId)}, TextMessage{response}});
			}
			else
			{
				LOG_WARNING("每日早安生成失败，今日跳过");
			}
		}

		// 到期提醒：逐条经模型以人格口吻渲染后私聊送达，模型失败时兜底直发原文
		for (const DueEvent &due : reminders.popDue(nowSeconds()))
		{
			std::string prompt = "现在是" + formatLocal(nowSeconds()) +
								 "。你之前收到用户的委托：" + due.content +
								 "。请把这个提醒转达给用户，简短自然。";
			if (due.late)
				prompt += "该提醒已错过原定触发时间（" + formatLocal(due.scheduled_at) +
						  "），请向用户说明这一点并致歉。";
			LOG_INFO("提醒到期：id=" + std::to_string(due.id) + "，user_id=" +
					 std::to_string(due.user_id));
			std::string response = chatService.replyInCharacter(due.user_id, prompt);
			if (response.empty())
			{
				LOG_WARNING("提醒渲染失败，兜底直发原文：id=" + std::to_string(due.id));
				response = "提醒（原定 " + formatLocal(due.scheduled_at) + "）：" + due.content;
			}
			sender.deliver(OutboundDelivery{
				DirectMessageTarget{std::to_string(due.user_id)}, TextMessage{response}});
		}

		// 观察通道亲密度落库（T7）：内部节流，30s 间隔或累计 delta≥100 才真正写
		perception.flushDue(nowSeconds());
		// 群内容库 TTL 清理（T7b）：删除 24h 前的消息行（镜像+磁盘）；关闭时无库
		if (groupContext != nullptr)
			groupContext->prune(nowSeconds());
		// 群列表刷新（T7c）：OneBot 就绪后拉取，失败冷却重试；关闭时无服务
		if (groupList != nullptr)
			groupList->poll(static_cast<std::int64_t>(now));
		// 话题状态机 pump（T7b）：时间衰减 + lull 检测，到期群经 evaluator 提交
		groupContextService.pump(nowSeconds());

		sleepWhileRunning(running, std::chrono::seconds(3));
	}
	LOG_INFO("定时任务线程已退出");
}

// 子线程
void workingThread(Message &messageClass, EventRouter &eventRouter,
				   PerceptionChannel &perception, GroupContextService &groupContext,
				   InboundMessage data, std::size_t maxMessageTokens)
{
	// notice/request 事件：不过消息过滤，按 key 路由给已注册 handler（v2.4.1 T6 起有订阅者）
	if (data.post_type != "message")
	{
		eventRouter.dispatch(data);
		return;
	}

	// UTF-8 下 1 汉字 ≈ 3 字节；OpenAI 分词器以汉字数计 token，这里按 3 倍粗略放宽上限
	if (data.payload_size_bytes >
		(maxMessageTokens * 3))
	{
		if (data.message_type == "group" && !messageClass.messageFilter(data))
		{
			LOG_INFO("群消息且非AT消息触发的错误警告，该消息将会不会被处理和发送...");
			return;
		}
		messageClass.sendError(data, "系统提示：消息长度超过最大限度，请减少单次发送的字符数量...");
		return;
	}

	// 群内容入库（T7b）：白名单群的 @ 与非 @ 消息都是群内容（超长的刷屏
	// 分支已在上文 return，不计入库）；服务内部有开关/白名单/群聊三重门控
	groupContext.observe(data);

	if (!messageClass.messageFilter(data))
	{
		// 非 @ 群消息等过滤失败路径：观察通道只看不说（T7，白名单外/关闭时内部 no-op）
		perception.observeMessage(data);
		return;
	}

	// @bot 已处理消息进互动计数
	perception.observeInteraction(data);
	messageClass.handleMessage(data);
}

void resourceCleanup(std::atomic<bool> &running, KeyedTaskScheduler &messageWorkers,
					 MemoryService &memoryService,
					 std::thread &timingThread, std::thread &transportThread,
					 std::thread &panelThread)
{
	running.store(false);
	LOG_INFO("正在停止消息执行器并等待活动任务退出...");
	messageWorkers.shutdown();
	LOG_INFO("消息执行器已退出");
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
	if (panelThread.joinable())
	{
		LOG_INFO("正在等待配置面板线程退出...");
		panelThread.join();
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

	LOG_INFO("当前Klein版本：" + std::string(KLEINBOT_VERSION_STRING));
	LOG_INFO("当前配置Schema版本：" + std::to_string(schemaVersion));
}

int main(int argc, char **argv)
{
	std::signal(SIGINT, signalHandler);
	std::signal(SIGTERM, signalHandler);
	std::atomic<bool> running{true};

	std::string configPath = ".config.json";
	bool checkConfigOnly = false;
	for (int index = 1; index < argc; ++index)
	{
		const std::string argument = argv[index];
		if (argument == "--version" || argument == "-V")
		{
			std::cout << VersionInfo::summary() << std::endl;
			return 0;
		}
		else if (argument == "--check-config")
			checkConfigOnly = true;
		else if (argument == "--config" && index + 1 < argc)
			configPath = argv[++index];
		else
		{
			std::cerr << "未知参数或缺少参数值：" << argument << std::endl;
			return -1;
		}
	}

	std::string bootstrapToken;
	if (ConfigTemplate::createIfMissing(configPath, bootstrapToken))
	{
		LOG_INFO("首次运行：已生成默认配置文件 " + configPath);
		LOG_INFO("Web 配置面板：http://127.0.0.1:" + std::to_string(kDefaultWebUiPort) + "/");
		LOG_INFO("面板访问令牌：" + bootstrapToken);
		LOG_WARNING("默认配置为占位骨架，请通过面板或直接编辑文件补全机器人 QQ、默认模型与通信配置后重启。");
	}

	ConfigLoader configLoader;
	ConfigLoadResult loadResult = configLoader.loadFile(configPath);
	logConfigDiagnostics(loadResult.diagnostics);
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
	ConfigSnapshotStore configStore(configPath, schema);
	const std::shared_ptr<const ConfigSnapshot> startupSnapshot = configStore.current();
	const RuntimeSettings &settings = startupSnapshot->runtime;
	init(settings.schemaVersion);
	const TransportConfig &transportConfig = settings.transport;
	LOG_INFO("当前通信模式：" + transportModeName(transportConfig.mode));
	ModelRegistry models(kModelRegistryPath);
	const std::string &dbPath = settings.storage.conversationDatabase;
	ConversationStore conversationStore(dbPath);
	ImageAssetStore imageAssetStore(dbPath, settings.storage.imageAssets);
	ReminderStore reminderStore(dbPath);
	// 构造即重建内存队列：24 小时内漏触发的提醒会补发，超窗的滚动或丢弃
	ReminderService reminderService(reminderStore);
	// 默认人格来自 source/soul.md，用户人格持久化在会话库 user_persona 表
	UserSessionService userSession(models, conversationStore, settings.bot, settings.chat,
	                                "source/soul.md");
	Dock dock(settings.dock, &running);
	MemoryService memoryService(dbPath, conversationStore, dock, models, settings.memory);
	userSession.setMemoryService(&memoryService);
	userSession.setImageAssetStore(&imageAssetStore);
	if (settings.bot.managerId != 0)
		userSession.ensureUserExists(settings.bot.managerId);
	std::unique_ptr<SearchProvider> webSearchProvider;
	std::unique_ptr<WebSearchAction> webSearchAction;
	std::unique_ptr<WebFetchAction> webFetchAction;
	ToolRegistry tools;
	bool globalVoice = settings.voice.enabled;
	ComputerStatus adminComputerStatus;
	GetCurrentModelAction getCurrentModelAction([&userSession](uint64_t userId)
		{ return userSession.getModelName(userId); });
	VoiceModeAction voiceModeAction(userSession, globalVoice);
	AdminControlAction adminControlAction(adminComputerStatus,
		globalVoice, [&configStore]()
		{
			ConfigReloadResult result = configStore.reload();
			logConfigDiagnostics(result.diagnostics);
			for (const ConfigChange &change : result.diff.changes())
			{
				LOG_INFO("配置变化：" + change.path + " [" +
					configChangeImpactName(change.impact) + "]");
			}
			return summarizeReload(result);
		});
	if (settings.webSearch.enabled)
	{
		webSearchProvider = std::make_unique<TavilySearchProvider>(settings.webSearch, &running);
		webSearchAction = std::make_unique<WebSearchAction>(*webSearchProvider, settings.webSearch);
		tools.registerTool(std::make_unique<ActionTool>(*webSearchAction));
		LOG_INFO("联网搜索工具已启用，Provider：" + settings.webSearch.provider);
	}
	if (settings.webFetch.enabled)
	{
		// 超长正文走默认小模型做"按问题摘录"，失败时 Action 自动降级为截断
		const std::string distillModel = settings.chat.defaultModel;
		webFetchAction = std::make_unique<WebFetchAction>(
			settings.webFetch, &running, WebFetchAction::HttpGet{},
				[&dock, &models, distillModel](const std::string &prompt)
				{
					const std::optional<ChatModel> modelPtr = models.find(distillModel);
					if (!modelPtr)
						return std::string{};
					ChatRequest request;
					request.system_prompt = "你是网页内容提取器，只输出与问题相关的原文摘录。";
					request.history.push_back({"user", prompt});
					const ChatResponse response =
						dock.RequestChat(*modelPtr, distillModel, request);
				if (response.cancelled || response.code != 200)
					return std::string{};
				return response.content;
			});
		tools.registerTool(std::make_unique<ActionTool>(*webFetchAction));
		LOG_INFO("网页抓取工具已启用");
	}
	tools.registerTool(std::make_unique<GetTimeTool>());
	tools.registerTool(std::make_unique<ActionTool>(getCurrentModelAction));
	tools.registerTool(std::make_unique<RecallConversationTool>(memoryService));
	tools.registerTool(std::make_unique<GenerateImageTool>(dock, imageAssetStore, settings.models.drawing));
	tools.registerTool(std::make_unique<InspectImageTool>(dock, imageAssetStore, settings.models.vision));
	tools.registerTool(std::make_unique<SendImageTool>(imageAssetStore));
	tools.registerTool(std::make_unique<ActionTool>(voiceModeAction));
	tools.registerTool(std::make_unique<ActionTool>(adminControlAction));
	SetReminderAction setReminderAction(reminderService);
	ListRemindersAction listRemindersAction(reminderService);
	CancelReminderAction cancelReminderAction(reminderService);
	tools.registerTool(std::make_unique<ActionTool>(setReminderAction));
	tools.registerTool(std::make_unique<ActionTool>(listRemindersAction));
	tools.registerTool(std::make_unique<ActionTool>(cancelReminderAction));
	std::string registeredTools;
	for (const std::string &toolName : tools.names())
	{
		if (!registeredTools.empty())
			registeredTools += ", ";
		registeredTools += toolName;
	}
	LOG_INFO("已注册模型工具：" + registeredTools);
	ChatService chatService(dock, userSession, models, tools, memoryService, settings.chat, settings.bot.managerId);
	InboundMessageQueue inboundQueue;
	OutboundMessageQueue outboundQueue;
	QueuedMessageSender messageSender(outboundQueue);
	OneBotEventDecoder oneBotEventDecoder;
	OneBotMessageEncoder oneBotMessageEncoder;
	// API 通道（T1 echo 关联）：WS 模式共用 WebSocketApiChannel，HTTP 模式用同步 HttpApiChannel
	WebSocketApiChannel webSocketApiChannel;
	std::unique_ptr<HttpApiChannel> httpApiChannel;
	if (transportConfig.mode == TransportMode::Http)
	{
		httpApiChannel = std::make_unique<HttpApiChannel>(
			transportConfig.http.apiBaseUrl, transportConfig.http.apiAuthToken,
			transportConfig.connectTimeoutMs, transportConfig.requestTimeoutMs, &running);
	}
	OneBotApiChannel &activeApiChannel = httpApiChannel != nullptr
											 ? *httpApiChannel
											 : static_cast<OneBotApiChannel &>(webSocketApiChannel);
	// 能力协商（T2）：pollingThread 探测一次实现端身份，T5/T6 按能力位启用
	CapabilityBroker capabilityBroker(activeApiChannel);
	// 私聊"正在输入"（T5）：仅聊天路径，能力位门控
	TypingIndicator typingIndicator(capabilityBroker, activeApiChannel);
	Voice voice(settings.voice, &running);
	CommandRegistry commandRegistry(settings.bot.managerId);
	commandRegistry.registryCommand(std::make_unique<HelpCommand>());
	commandRegistry.registryCommand(std::make_unique<ModelListCommand>(models));
	commandRegistry.registryCommand(std::make_unique<SearchSongsCommand>());
	commandRegistry.registryCommand(std::make_unique<QueryModelCommand>(getCurrentModelAction));
	commandRegistry.registryCommand(std::make_unique<GeneratePictureCommand>(dock, settings.models.drawing));
	commandRegistry.registryCommand(std::make_unique<ResetChatCommand>(userSession));
	commandRegistry.registryCommand(std::make_unique<ResetContextCommand>(userSession));
	commandRegistry.registryCommand(std::make_unique<SetSoulCommand>(userSession));
	commandRegistry.registryCommand(std::make_unique<SwitchModelCommand>(userSession, models));
	commandRegistry.registryCommand(std::make_unique<VoiceSwitchCommand>(voiceModeAction));
	commandRegistry.registryCommand(std::make_unique<RemoveContextCommand>(userSession));
	commandRegistry.registryCommand(std::make_unique<AdminCommand>(adminControlAction));
	// 观察通道（T7）：只看不说、0 LLM；白名单起步，功能关闭时不建库不动用户数据。
	// 须在 Message 之前构造：Message 持其指针在 @ 消息上注入话题注记
	std::unique_ptr<PerceptionStore> perceptionStore;
	std::unique_ptr<GroupContextStore> groupContextStore;
	if (settings.perception.observing())
	{
		perceptionStore = std::make_unique<PerceptionStore>(dbPath);
		groupContextStore = std::make_unique<GroupContextStore>(dbPath);
		LOG_INFO("观察通道已启用，白名单群 " + std::to_string(settings.perception.observeGroups.size()) +
				 " 个（群内容短时缓冲：每群300条/24小时）");
	}
	// 群列表缓存（T7c）：白名单选择器的配置辅助，与观察通道开关解耦——
	// 它的用途恰是帮用户从零挑白名单，绑在"已启用"上会死锁（没配置→没数据→没法配置）。
	// 纯缓存表无隐私顾虑，只要有面板就构造；OneBot 就绪后由 pollingThread 拉取
	std::unique_ptr<GroupListService> groupListService =
		std::make_unique<GroupListService>(dbPath, activeApiChannel, settings.perception);
	PerceptionChannel perceptionChannel(settings.perception, perceptionStore.get());
	GroupContextService groupContextService(settings.perception, settings.bot,
											groupContextStore.get(), &messageSender);
	groupContextService.setSummarizer([&chatService](const std::string &systemPrompt,
													 const std::string &userPrompt)
		{ return chatService.buildOnce(systemPrompt, userPrompt); });
	if (settings.bot.managerId != 0)
	{
		// 主动发言必须是她的声音：responder 走 replyInCharacter（管理员会话
		// 装配人格，与事件回应同源）；无管理员则不启用主动发言
		groupContextService.setResponder(
			[&chatService, managerId = settings.bot.managerId](const std::string &prompt)
			{ return chatService.replyInCharacter(managerId, prompt); });
	}
	Message messageClass(dock, userSession, chatService, messageSender, imageAssetStore,
		commandRegistry, voice, settings.message, settings.models.vision,
		globalVoice, &typingIndicator, &perceptionChannel, &groupContextService);
	// notice/request 事件路由（v2.4.1 T4）+ 好友申请通报（T6）：
	// handler 在 worker 内执行（事件按 user_id 占 lane），生命周期由 main 作用域保证
	FriendRequestNotifier friendRequestNotifier(messageSender, activeApiChannel,
												settings.bot.managerId);
	EventRouter eventRouter;
	eventRouter.subscribe("request.friend", friendRequestNotifier);
	KeyedTaskScheduler messageWorkers(
		settings.messageExecution,
		[](std::exception_ptr error)
		{
			try
			{
				if (error)
					std::rethrow_exception(error);
			}
			catch (const std::exception &exception)
			{
				LOG_ERROR("消息处理任务异常：" + std::string(exception.what()));
			}
			catch (...)
			{
				LOG_ERROR("消息处理任务发生未知异常");
			}
		});
	// 主动评估任务提交：按群占独立 lane（高位标志位避免与 user_id 撞车），
	// 同群串行、跨群并行，评估在 worker 内执行不阻塞 pollingThread
	groupContextService.setEvaluator([&messageWorkers, &groupContextService](
										  std::uint64_t groupId)
		{
			constexpr std::uint64_t kGroupLaneFlag = 0x8000'0000'0000'0000ULL;
			messageWorkers.submit(groupId | kGroupLaneFlag,
				[&groupContextService, groupId]()
				{ groupContextService.evaluateGroup(groupId); });
		});
	LOG_INFO("消息执行器线程：初始 " +
		std::to_string(settings.messageExecution.initialWorkerThreads) +
		"，最大 " + std::to_string(settings.messageExecution.maxWorkerThreads) +
		"，队列容量 " + std::to_string(settings.messageExecution.maxPendingMessages));
	std::thread timingThread(pollingThread, std::ref(chatService), std::ref(messageSender), std::ref(reminderService), std::ref(capabilityBroker), std::ref(perceptionChannel), groupContextStore.get(), groupListService.get(), std::ref(groupContextService), settings.bot.managerId, std::cref(running));

	std::thread transportThread;
	switch (transportConfig.mode)
	{
	case TransportMode::ForwardWebSocket:
		transportThread = std::thread(
			MyWebSocket::connectWebSocket,
			std::cref(transportConfig.forwardWebSocket),
			std::ref(inboundQueue), std::ref(outboundQueue),
			std::ref(webSocketApiChannel),
			std::cref(oneBotEventDecoder), std::cref(oneBotMessageEncoder),
			std::cref(running));
		break;
	case TransportMode::ReverseWebSocket:
		transportThread = std::thread(
			MyReverseWebSocket::connectReverseWebSocket,
			std::cref(transportConfig.reverseWebSocket),
			std::ref(inboundQueue), std::ref(outboundQueue),
			std::ref(webSocketApiChannel),
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

	std::thread panelThread;
	if (settings.webUi.enabled)
	{
		panelThread = std::thread(
			ConfigPanelServer::run,
			settings.webUi, configPath, std::ref(configStore), std::ref(models),
			groupListService.get(), std::cref(running));
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
			auto message = std::make_shared<InboundMessage>(std::move(*msg));
			const TaskSubmitResult submitResult = messageWorkers.submit(
				message->user_id,
				[&messageClass, &eventRouter, &perceptionChannel, &groupContextService,
				 maxMessageTokens = settings.chat.maxMessageTokens,
				 message]() mutable {
				workingThread(messageClass, eventRouter, perceptionChannel, groupContextService, std::move(*message), maxMessageTokens);
			});
			if (submitResult == TaskSubmitResult::Full)
			{
				LOG_WARNING("消息执行队列已满，拒绝新的入站消息");
				if (messageClass.messageFilter(*message))
					messageClass.sendError(*message, "系统繁忙，请稍后重试。");
			}
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}

	resourceCleanup(running, messageWorkers, memoryService, timingThread,
					transportThread, panelThread);
	Log::getInstance().shutdown();

	return 0;
}
