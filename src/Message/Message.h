#ifndef MESSAGE_H
#define MESSAGE_H

#include "../TimingTast/TimingTast.h"
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

// 消息类
class Message
{
public:
	// MessageSenderPort 由 main.cpp 装配并注入，Message 自身不负责生命周期
	explicit Message(Dock &dock, UserSessionService &userSession, ChatService &chatService,
                     MessageSenderPort &sender, ImageAssetStore &imageAssetStore,
                     CommandRegistry &registry, Voice &voice, MessageOptions options,
                     ModelEndpointOptions visionModel, bool &globalVoice,
                     bool &accessibilityChat);

	/**
	 * @brief 消息过滤，对某些消息进行过滤
	 *
	 * @param message_type 	消息类型
	 * @param message	具体消息
	 *
	 */
	bool messageFilter(std::string message_type, std::string message);

	// 处理一条入站消息：分类 → 命令 / Vision / 普通对话，结果直接经由 sender 发出
	void handleMessage(const InboundMessage &current_data);

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

	// 把 OutboundMessage 按 message_type 路由到 send_private / send_group
	void dispatch(const InboundMessage &data, const OutboundMessage &msg);

	// 文本超长自动分段发送（utf-8 安全切分）
	void dispatchText(const InboundMessage &data, const std::string &text);
	// 在下面添加新的函数用于拓展其他内容...

private:
	std::string help_message;

	bool &accessibility_chat;													 // true为开启
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
};

#endif // MESSAGE_H
