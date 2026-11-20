#ifndef MESSAGE_H
#define MESSAGE_H

#include "../ModelApiCaller/Dock.hpp"
#include "../ComputerStatus/ComputerStatus.h"
#include "../Command/CommandRegistry.h"
#include "../UserSession/UserSessionService.h"
#include "../Port/MessageSenderPort.h"
#include "../Port/InboundMessage.h"
#include "../Port/OutboundMessage.h"
#include "../ChatService/ChatService.h"
#include "../Asset/ImageAssetStore.h"
#include "../Action/Action.h"
#include "MessageOptions.h"
#include "../Affect/AffectOptions.h"
#include "../ModelApiCaller/ModelEndpointOptions.h"
#include "../utils/Live.h"
#include <iostream>
#include <random>
#include <memory>
#include <unordered_set>

namespace affect
{
class DecisionPolicy;
class DecisionRecorder;
class DeployerFacts;
}

// 消息类
class Message
{
public:
    // MessageSenderPort 由 main.cpp 装配并注入，Message 自身不负责生命周期
    // typingIndicator 可空：注入时私聊 LLM 路径触发"正在输入"（T5），测试可传 nullptr
    // perception 可空：注入时白名单群被 @ 的回复带上话题背景注记（T7），关闭时内部 no-op
    // groupContext 可空：注入时白名单群启用内容级上下文装配与收敛契约（T7b）
    // visionModel：视觉端点来源，每次调用时取一次（models.vision 可只填注册名）
    // options / affectOptions 都是组合根的活值（ADR-023，只读）：
    // 群聊开关在每次过滤时取，affect（策略选择与 enforce）在每次决策时取一次；
    // decisionPolicy 是组合根的策略外壳，每条消息按快照 resolve 一次。
    // deployerFacts 可空（= 不加事实卡，经历层 E0）：非空且当条消息的 affect.enabled 为真时，群 @
    // 回复的部署者事实段接在群聊收敛契约之后（进 system），现场事实并入本轮尾注；私聊不加。
    // 语音不在这里决定（2026-11-16）：她要用语音时调用 send_voice 工具，语音作为工具出站消息送达
    explicit Message(Dock &dock, UserSessionService &userSession, ChatService &chatService,
                     MessageSenderPort &sender, ImageAssetStore &imageAssetStore,
                     CommandRegistry &registry, const Live<MessageOptions> &options,
                     EndpointProvider visionModel, const Live<AffectOptions> &affectOptions,
                     class TypingIndicator *typingIndicator = nullptr,
                     class PerceptionChannel *perception = nullptr,
                     class GroupContextService *groupContext = nullptr,
                     affect::DecisionRecorder *decisionRecorder = nullptr,
                     ImageAssetStore *sharedAssets = nullptr,
                     affect::DecisionPolicy *decisionPolicy = nullptr,
                     const affect::DeployerFacts *deployerFacts = nullptr);

	/**
	 * @brief 消息过滤：群聊触发门槛与私聊放行
	 *
	 * 群消息必须显式 @ bot 才处理（成本与防骚扰的闸门）；
	 * 判断基于解码器解析的 mentioned_ids 结构化字段（v2.4.1 起替代
	 * 旧版 raw_message 的 CQ:at 字符串匹配，顺带消除"正文含 bot
	 * QQ 号数字即误触发"的缺陷）；私聊直接放行
	 *
	 * @param data 入站消息
	 *
	 */
	bool messageFilter(const InboundMessage &data);

	// 处理一条入站消息：分类 → 命令 / Vision / 普通对话，结果直接经由 sender 发出
	// burstNote（R2.2 私聊连发）：同一连发里这条之后还有哪些消息的事实性注记
	// （burstFollowUpNote 产出），只进本轮请求尾部；空 = 与此前逐字相同。
	// 每条消息仍各自一次回复（N 进 N 出），不改回复条数、分段与语音路径
	void handleMessage(const InboundMessage &current_data, const std::string &burstNote = {});

	// 这条消息会不会被当作 #命令 处理（只匹配不执行）。组合根据此把命令消息排除在
	// 群内容缓冲与话题会话之外——命令往来是协议层对话，不是群聊内容（2026-10-28 修复：
	// 群里 "@她 #坏" 曾被会话当成点名而多回一句）
	bool isCommand(const InboundMessage &data) const;

	// 错误消息分发：直接构造 TextMessage 发回去（替代 main.cpp 旧的 isErrorTransfer 分支）
	void sendError(const InboundMessage &current_data, const std::string &text);

	~Message();

private:
	// 消息意图分类
	enum class Intent
	{
		Chat,
		Command,
		Vision,
		SystemEvent
	};
	Intent classify(const InboundMessage &data);

	/**
	 * @brief 调用视觉模型对图片进行分析
	 *
	 * @param user_id	用户QQ
	 * @param message 	具体消息
	 * @param message_data_url 		图片链接
	 *
	 * @return 			返回处理后的内容
	 *
	 */
	std::string provideImageRecognition(const uint64_t user_id, const std::string &message, const std::string &message_data_url);

	/**
	 * @brief 将传入进去的数据转为bash64编码，最后data保存base64编码
	 *
	 * @param input 	数据流
	 *@return 			返回处理完毕后的base64编码
	 */
	std::string dataToBase64(const std::string &input);

	/**
	 * @brief 将传入进去的文本转为URL编码
	 *
	 * @param input 	文本数据
	 *@return 			返回处理完毕后的URL编码
	 */
	std::string encodeToURL(const std::string &input);

	// 把 OutboundMessage 按 message_type 路由到 send_private / send_group。
	// withReplyContext=false 时不带引用+@（同一轮的第一条出站之后）。
	// correlationId 非空时该投递要回执（R0.2）。observeOutbound=false 时不把这条出站记为
	// 她的群聊发言（命令确认、系统错误提示都不是对话内容）
	void dispatch(const InboundMessage &data, const OutboundMessage &msg,
				  bool withReplyContext = true, const std::string &correlationId = {},
				  bool observeOutbound = true);

	// 文本超长自动分段发送（utf-8 安全切分）；群聊只有第一段带引用+@。
	// 关联键每段都带：首段用裸 correlationId（驱动执行记录状态），后续段用
	// "correlationId#序号"，回执到达后 message_id 都聚合到同一条执行记录——
	// 引用任意一段打反馈都能精确命中
	void dispatchText(const InboundMessage &data, const std::string &text,
					  bool withReplyContext = true, const std::string &correlationId = {});
	// 在下面添加新的函数用于拓展其他内容...

private:
	std::string help_message;

	Dock &dock;
	UserSessionService &userSession;
	ChatService &chatService;
	MessageSenderPort &sender;
	ImageAssetStore &imageAssetStore;
	CommandRegistry &registry;
    const Live<MessageOptions> &options;
    EndpointProvider visionModel;
    class TypingIndicator *typingIndicator = nullptr; // 可空：未注入则无输入状态提示
    class PerceptionChannel *perception = nullptr;    // 可空：未注入则无话题注记
    class GroupContextService *groupContext = nullptr; // 可空：未注入则无内容级群上下文
    affect::DecisionRecorder *decisionRecorder = nullptr; // 可空：M1 影子决策记录
    // 可空（= 基线）：R1 决策层。群聊 @ 且非基线策略时，影子期在投递之后跑 judge 并记录计划
    affect::DecisionPolicy *decisionPolicy = nullptr;
    const Live<AffectOptions> &affectOptions; // enforce：false = 影子期，计划只进记录
    const affect::DeployerFacts *deployerFacts = nullptr; // 可空：事实卡（经历层 E0）
    // 可空：群资产库（ADR-019）。群聊的当前图片进这里（24h），不进发送者个人库；
    // 工具经 ToolContext.shared_scope_id 先查它
    ImageAssetStore *sharedAssets = nullptr;
};

#endif // MESSAGE_H
