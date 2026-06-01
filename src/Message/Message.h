#ifndef MESSAGE_H
#define MESSAGE_H

#include "../TimingTast/TimingTast.h"
#include "../ModelApiCaller/Dock.hpp"
#include "../ModelApiCaller/Voice/Voice.h"
#include "../ComputerStatus/ComputerStatus.h"
#include "../Command/CommandRegistry.h"
#include "../UserSession/UserSessionService.h"
#include <iostream>
#include <random>
#include <memory>
#include <unordered_set>

/*
 * 任何功能函数，各司其职，需要完成自己的任务，例如textToVoice函数是用于完成文本转语音的操作，
 * 那么它返回的值，就是该函数所返回的语音数据（base64），并封装好CQ码。
 */

// 消息类
class Message
{
public:
	Message();

	/**
	 * @brief 消息过滤，对某些消息进行过滤
	 *
	 * @param message_type 	消息类型
	 * @param message	具体消息
	 *
	 */
	bool messageFilter(std::string message_type, std::string message);

	/**
	 * @brief 处理私聊消息
	 *
	 * @param private_id 	用户QQ
	 * @param message 	该用户所发送的信息
	 * @param message_type 消息类型
	 * @return 			返回封装好的CQ码
	 */
	void handleMessage(JsonData &current_data);

	~Message();

private:
	/**
	 * @brief 添加用户
	 *
	 * @param message 	用户QQ
	 *
	 */
	void questPictureID(std::string &message);

	/**
	 * @brief 个性化聊天（对接人工智能模块）
	 *
	 * @param data 	聚合消息
	 *
	 */
	std::string characterMessage(const JsonData &data);

	/**
	 * @brief 音乐分享
	 *
	 * @param message 	具体消息
	 * @param platform 	音乐来自哪个平台，1为网易云...
	 *
	 * @return 			返回封装好的CQ码或者返回空字符串
	 */
	std::string musicShareMessage(const std::string &message, short platform);

	/**
	 * @brief 艾特群友
	 *
	 * @param message 	具体消息
	 * @param user_id 	群友QQ
	 *
	 */
	std::string atUserMassage(std::string message, const uint64_t user_id);

	/**
	 * @brief 艾特所有人
	 *
	 * @param message 	具体消息
	 *
	 */
	void atAllMessage(std::string &message);

	/**
	 * @brief 调用图片修复接口
	 *
	 * @param message 	具体消息
	 *
	 */
	void call_fixImageSizeTo4K(std::string &message);

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

	/**
	 * @brief 将文本转为语音
	 *
	 * @param text 	文本
	 * @return  正常返回封装好的CQ码，否则返回错误信息
	 */
	std::string textToVoice(const std::string &text);

	/**
	 * @brief 使用图片生成模型生成图片
	 *
	 * @param user_id 	用户QQ
	 * @param text		文本
	 *
	 * @return 			返回带有CQ码的数据
	 */
	std::string provideImageCreation(const uint64_t user_id, const std::string &text);

	/**
	 * @brief 去掉群聊内容的CQ码
	 *
	 * @param message 	传入进去的消息
	 * @return 			返回处理完毕的数据
	 */
	std::string removeGroupCQCode(const std::string &message);

	/**
	 * @brief 调用stable diffusion 实现图像创建
	 *
	 * @param message 	 提示
	 * @return 			返回处理完毕的数据
	 */
	std::string SDImageCreation(const std::string &message);

	/**
	 * @brief 刷新模型配置文件
	 *
	 */
	void readModelName();
	// 在下面添加新的函数用于拓展其他内容...

private:
	std::string help_message;
	// std::string default_personality;
	// std::string users_message_format;
	// std::string bot_message_format;
	// std::string system_message_format;
	// short default_message_line;
	bool accessibility_chat;													 // true为开启
	bool global_Voice;															 // true为开启
	std::vector<std::pair<std::string, std::string>> LightweightPersonalityList; // 轻量型人格
	// std::unordered_map<uint64_t, Person> *user_messages;
	// std::unique_ptr<std::unordered_map<uint64_t, Person>> user_messages;						  // key = QQ,second = 用户信息
	// std::mutex mutex_message;																	  // message类的锁
	std::unique_ptr<ComputerStatus> PCStatus;													  // 监控计算机状态
	std::vector<std::pair<std::unordered_set<std::string>, std::vector<std::string>>> chatModels; // 存储模型   first存储该端点的模型名称，second存储该模型的api、端点、API标准
	std::unique_ptr<Voice> voice;																  // 语音识别模块
	std::unique_ptr<Dock> dock;																	  // 对话模块

	// 新增
	CommandRegistry registry;
	UserSessionService userSession;
	// std::unique_ptr<LLMService> llmservice;
};

#endif