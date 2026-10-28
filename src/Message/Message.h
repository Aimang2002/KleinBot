#ifndef MESSAGE_H
#define MESSAGE_H

#include "../ModelApiCaller/Dock.hpp"
#include "../ModelApiCaller/Voice/Voice.h"
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
#include "../ModelApiCaller/ModelEndpointOptions.h"
#include <iostream>
#include <random>
#include <memory>
#include <unordered_set>

namespace affect
{
class DecisionRecorder;
}

// 消息类
class Message
{
public:
    // MessageSenderPort 由 main.cpp 装配并注入，Message 自身不负责生命周期
    // typingIndicator 可空：注入时私聊 LLM 路径触发"正在输入"（T5），测试可传 nullptr
    // perception 可空：注入时白名单群被 @ 的回复带上话题背景注记（T7），关闭时内部 no-op
    // groupContext 可空：注入时白名单群启用内容级上下文装配与收敛契约（T7b）
    explicit Message(Dock &dock, UserSessionService &userSession, ChatService &chatService,
                     MessageSenderPort &sender, ImageAssetStore &imageAssetStore,
                     CommandRegistry &registry, Voice &voice, MessageOptions options,
                     ModelEndpointOptions visionModel, bool &globalVoice,
                     class TypingIndicator *typingIndicator = nullptr,
                     class PerceptionChannel *perception = nullptr,
                     class GroupContextService *groupContext = nullptr,
                     affect::DecisionRecorder *decisionRecorder = nullptr,
                     ImageAssetStore *sharedAssets = nullptr);

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
	void handleMessage(const InboundMessage &current_data);

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

	// 文本 → 语音：成功返回本地 wav 路径，失败返回空字符串
	// 协议封装（变成 VoiceMessage）由调用方完成，本函数只做 TTS
	std::string textToVoice(const std::string &text);

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

	bool &global_Voice;															 // true为开启

	Dock &dock;
	UserSessionService &userSession;
	ChatService &chatService;
	MessageSenderPort &sender;
	ImageAssetStore &imageAssetStore;
	CommandRegistry &registry;
    Voice &voice;
    MessageOptions options;
    ModelEndpointOptions visionModel;
    class TypingIndicator *typingIndicator = nullptr; // 可空：未注入则无输入状态提示
    class PerceptionChannel *perception = nullptr;    // 可空：未注入则无话题注记
    class GroupContextService *groupContext = nullptr; // 可空：未注入则无内容级群上下文
    affect::DecisionRecorder *decisionRecorder = nullptr; // 可空：M1 影子决策记录
    // 可空：群资产库（ADR-019）。群聊的当前图片进这里（24h），不进发送者个人库；
    // 工具经 ToolContext.shared_scope_id 先查它
    ImageAssetStore *sharedAssets = nullptr;
};

#endif // MESSAGE_H
