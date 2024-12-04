#ifndef MESSAGE_H
#define MESSAGE_H

/*
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <ctime>
#include <iomanip>
#include <filesystem>
#include <cstdio>
*/
#include <random>
#include "../TimingTast/TimingTast.h"
#include "../Database/Database.h"
// #include "../JsonParse/JsonParse.h"
// #include "../ConfigManager/ConfigManager.h"
#include "../ComputerStatus/ComputerStatus.h"
#include "../ModelApiCaller/StableDiffusion/StableDiffusion.h"
#include "../ModelApiCaller/Realesrgan/Realesrgan.h"
#include "../ModelApiCaller/Dock.hpp"
#include "../ModelApiCaller/Voice/Voice.h"

// using namespace std;

extern ConfigManager &CManager;
extern JsonParse &JParsingClass;
extern TimingTast &TTastClass;

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
	 */
	// string handleMessage(const uint64_t private_id, string message, string message_type);
	std::string handleMessage(JsonData &data);

	~Message();

private:
	/**
	 * @brief 添加用户，用于添加新的用户
	 *
	 * @param user_id 	用户QQ
	 * @return 			若返回true则表示创建成功
	 */
	bool addUsers(uint64_t user_id);

	/**
	 * @brief 添加用户
	 *
	 * @param message 	用户QQ
	 *
	 */
	void questPictureID(std::string &message);

	/**
	 * @brief 语音，发送语音
	 *
	 * @param message 	具体消息
	 *
	 */
	void SpeechSound(std::string &message);

	/**
	 * @brief 个性化聊天（对接人工智能模块）
	 *
	 * @param user_id 	具体消息
	 * @param message	用户QQ
	 *
	 */
	// void characterMessage(uint64_t &user_id, string &message);
	void characterMessage(JsonData &data);

	/**
	 * @brief 音乐分享
	 *
	 * @param message 	具体消息
	 * @param platform 	音乐来自哪个平台，1为网易云...
	 *
	 */
	void musicShareMessage(std::string &message, short platform);

	/**
	 * @brief 表情包
	 *
	 * @param message	具体消息
	 *
	 */
	void facePackageMessage(std::string &message);

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
	 * @brief 设置人格,可设置不同人格
	 *
	 * @param roleName 	人格名称
	 * @param user_id 	用户QQ
	 *
	 */
	void setPersonality(std::string &roleName, const uint64_t user_id);

	/**
	 * @brief 设置人格(重载版本)
	 *
	 * @param roleName 	人格名称
	 * @param user_id 	用户QQ
	 * @param param3	int类型占位符
	 *
	 */
	void setPersonality(std::string &roleName, const uint64_t user_id, int);

	/**
	 * @brief 重置对话，将会清空所有上下文对话
	 *
	 * @param resetResult 	重置结果
	 * @param user_id 	用户QQ
	 *
	 */
	void resetChat(std::string &resetResult, uint64_t user_id);

	/**
	 * @brief 管理员终端，设置管理员命令
	 *
	 * @param message 	具体消息
	 * @param user_id 	用户QQ
	 *
	 */
	bool adminTerminal(std::string &message, const uint64_t user_id);

	/**
	 * @brief 管理员权限验证
	 *
	 * @param user_id 	管理员QQ
	 *
	 */
	bool permissionVerification(const uint64_t user_id);

	/**
	 * @brief 切换人工智能模型
	 *
	 * @param message 	具体消息
	 * @param user_id 	用户QQ
	 *
	 */
	void switchModel(std::string &message, const uint64_t user_id);

	/**
	 * @brief 调用图片修复接口
	 *
	 * @param message 	具体消息
	 *
	 */
	void call_fixImageSizeTo4K(std::string &message);

	/**
	 * @brief 调用GPT4-VISION模型
	 *
	 * @param user_id	用户QQ
	 * @param message 	具体消息
	 *
	 */
	bool provideImageRecognition(const uint64_t user_id, std::string &message, std::string &type);

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
	 * @return  返回-1.处理有误；
				返回1，使用路径传输;
				返回2，使用base64编码传输;
	 */
	int textToVoice(std::string &text, std::string &type);

	/**
	 * @brief 使用dall-e-3模型生成图片
	 *
	 * @param user_id 	用户QQ
	 * @param text		文本
	 */
	bool provideImageCreation(const uint64_t user_id, std::string &text);

	/**
	 * @brief 去掉群聊内容的CQ码
	 *
	 * @param message 	传入进去的消息
	 */
	bool removeGroupCQCode(std::string &message);

	/**
	 * @brief 移除上一次对话
	 *
	 * @param user_id 	需要进行该操作的QQ号
	 */
	std::string removePreviousContext(const uint64_t user_id);

	/**
	 * @brief 调用stable diffusion 实现图像创建
	 *
	 * @param message 	 提示
	 */
	void SDImageCreation(std::string &message);
	// 在下面添加新的函数用于拓展其他内容...

private:
	std::string help_message;
	std::string default_personality;
	std::string users_message_format;
	std::string bot_message_format;
	std::string system_message_format;
	short default_message_line;
	bool accessibility_chat;													 // true为开启
	bool global_Voice;															 // true为开启
	std::vector<std::pair<std::string, std::string>> LightweightPersonalityList; // 轻量型人格
	std::map<uint64_t, Person> *user_messages;									 // key = QQ,second = 用户信息
	std::mutex mutex_message;													 // message类的锁
	ComputerStatus *PCStatus;													 // 监控计算机状态
	std::vector<std::pair<std::string, std::string>> chatModels;				 // 存储模型名称   first存储模型名称，second存储模型厂商
	Voice *voice;																 // 语音识别模块
};

#endif