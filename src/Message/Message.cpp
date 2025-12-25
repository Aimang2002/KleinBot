#include "../ModelApiCaller/StableDiffusion/StableDiffusion.h"
#include "../ModelApiCaller/Realesrgan/Realesrgan.h"
#include "../ModelApiCaller/Dock.hpp"
#include "../submodules/CloudMusicID/CloudMusicID.h"
#include "Message.h"
#include <iomanip>

std::mt19937 mt_rand(1000);

Message::Message()
{
	srand((unsigned int)time(NULL));

	// 服务器状态类初始化
#ifdef DEBUG
	LOG_DEBUG("服务器状态初始化...");
#endif
	this->PCStatus = std::make_unique<ComputerStatus>();
	this->voice = std::make_unique<Voice>();
	this->dock = std::make_unique<Dock>();

	// 内置成员属性初始化
	this->user_messages = new std::unordered_map<uint64_t, Person>;
	this->accessibility_chat = ConfigManager::getInstance().configVariable("GLOBAL_VOICE") == "true" ? true : false;
	this->global_Voice = ConfigManager::getInstance().configVariable("ACCESSIBLITY_CHAT") == "true" ? true : false;
	this->system_message_format = R"({"role": "user", "content": ")"; // system
	this->bot_message_format = R"({"role": "assistant", "content": ")";
	this->users_message_format = R"({"role": "user", "content": ")";
	this->default_personality = "You are my assistant, your name is " + ConfigManager::getInstance().configVariable("QBOT_NAME") + "\"},";
	this->default_message_line = 2;

// 载入模型名称
#ifdef DEBUG
	LOG_DEBUG("正在载入模型名称...");
#endif
	this->readModelName();

	// 轻量型人格初始化
#ifdef DEBUG
	LOG_DEBUG("轻量型人格初始化...");
#endif
	std::string path = ConfigManager::getInstance().configVariable("PERSONALITY_PATH") + "personality.txt";
	std::ifstream ifs(path);
	if (!ifs.is_open())
	{
		perror("轻量型人格初始化失败");
		LOG_FATAL("失败！");
	}
	else
	{
		std::string line;
		while (!ifs.eof())
		{
			std::getline(ifs, line);
			size_t pos = line.find("|");
			if (pos != line.npos)
			{
				std::string key = line.substr(0, pos);
				std::string value = line.substr(pos + 1);
				this->LightweightPersonalityList.push_back(make_pair(key, value));
			}
			else
			{
				perror("中断读取，原始数据有误！");
				break;
			}
		}
		ifs.close();
	}

	// 初始化管理员
	uint64_t manager_qq = std::stoll(ConfigManager::getInstance().configVariable("MANAGER_QQ"));
	this->addUsers(manager_qq);
}

bool Message::addUsers(uint64_t user_id)
{
	if (user_messages->find(user_id) == user_messages->end())
	{
		// 添加默认数据
		std::vector<std::pair<std::string, time_t>> userDefault;
		std::pair<std::string, time_t> p;
		p.first = this->users_message_format + this->default_personality;
		p.second = time(nullptr); // 获取当前时间
		userDefault.push_back(p);

		p.first = this->bot_message_format + "OK!I will use Chinses answer\"},"; // 设置assistant信息
		userDefault.push_back(p);

		// 创建用户
		Person person;
		person.user_chatHistory = userDefault;

		std::pair<std::string, std::vector<std::string>> models;
		models.first = ConfigManager::getInstance().configVariable("DEFAULT_MODEL"); // 默认模型
		models.second.push_back(ConfigManager::getInstance().configVariable("DEFAULT_MODEL_API_KEY"));
		models.second.push_back(ConfigManager::getInstance().configVariable("DEFAULT_MODEL_ENDPOINT"));
		models.second.push_back(ConfigManager::getInstance().configVariable("DEFAULT_MODEL_APISTANDARD"));
		person.user_models = models;
		person.isOpenVoiceMode = false;
		person.temperature = ConfigManager::getInstance().configVariable("temperature");
		person.top_p = ConfigManager::getInstance().configVariable("top_p");
		person.frequency_penalty = ConfigManager::getInstance().configVariable("frequency_penalty");
		person.presence_penalty = ConfigManager::getInstance().configVariable("presence_penalty");

		std::lock_guard<std::mutex> lock(mutex_message);
		this->user_messages->insert(std::make_pair(user_id, person));
		return true;
	}
	return false;
}

void Message::handleMessage(JsonData &current_data)
{
	try
	{
		// type类型默认为text，如需要更改需要重新指定
		current_data.type = "text";

		// 新建用户
		if (this->user_messages->find(current_data.user_id) == this->user_messages->end())
		{
			this->addUsers(current_data.user_id);
		}

		// 判断是否是群消息
		if (current_data.message_type == "group")
		{
			// 移除CQ码
			this->removeGroupCQCode(current_data.raw_message);
		}

		// 显示用户输入的问题
		std::cout << "[" << current_data.message_type << "]" << current_data.user_id << ":" << current_data.raw_message << std::endl;

		// 管理员权限判断
		if (this->permissionVerification(current_data.user_id))
		{
			// 管理员可操作的命令
			auto [success, message_] = this->adminTerminal(current_data.raw_message, current_data.user_id);
			if (success)
			{
				current_data.raw_message = message_;
			}
			// 否则不是管理员的消息
		}

		// 内置回复判断
		if (current_data.raw_message.find("#图片超分") != current_data.raw_message.npos)
		{
			this->call_fixImageSizeTo4K(current_data.raw_message);
			current_data.type = "CQ";
		}
		else if (current_data.raw_message.find("CQ:image") != std::string::npos && !current_data.message_data_url.empty())
		{
			current_data.raw_message = this->provideImageRecognition(current_data.user_id, current_data.raw_message, current_data.message_data_url);
		}
		else if (current_data.raw_message.find("#搜歌:") != std::string::npos || current_data.raw_message.find("#搜歌：") != std::string::npos)
		{
			current_data.type = "CQ";
			current_data.raw_message = musicShareMessage(current_data.raw_message, 1);
		}
		else if (current_data.raw_message.compare("#帮助") == 0)
		{
			std::ifstream ifs(ConfigManager::getInstance().configVariable("HELP_PATH"));
			if (!ifs.is_open())
			{
				LOG_ERROR("帮助文件打开失败！请检查...");
				current_data.raw_message = "正在编辑中 .>_<.";
			}
			else
			{
				current_data.raw_message.assign((std::istreambuf_iterator<char>(ifs.rdbuf())), std::istreambuf_iterator<char>());
				ifs.close();
			}
		}
		else if (!strcmp(current_data.raw_message.c_str(), "#人格帮助"))
		{
			std::ifstream ifs(ConfigManager::getInstance().configVariable("HELP_PERSONALITY_PATH"));
			if (!ifs.is_open())
			{
				LOG_ERROR("file open failed!");
				current_data.raw_message = "正在编辑中 .>_<.";
			}
			else
			{
				current_data.raw_message.assign((std::istreambuf_iterator<char>(ifs.rdbuf())), std::istreambuf_iterator<char>());
				ifs.close();
			}
		}
		else if (current_data.raw_message.find("#设置人格:") != std::string::npos || current_data.raw_message.find("#设置人格：") != std::string::npos)
		{
			auto [success, result] = this->setPersonality(current_data.raw_message, current_data.user_id);
		}
		else if (current_data.raw_message.find("#轻量型人格:") != std::string::npos ||
				 current_data.raw_message.find("#轻量型人格：") != std::string::npos)
		{
			auto [success, result] = this->setPersonality(current_data.raw_message, current_data.user_id, 1);
		}
		else if (current_data.raw_message.find("#人格还原") != std::string::npos)
		{
			auto [success, result] = this->setPersonality(current_data.raw_message, current_data.user_id);
		}
		else if (current_data.raw_message.compare("#重置对话") == 0)
		{
			current_data.raw_message = this->resetChat(current_data.user_id);
		}
		else if (current_data.raw_message.find("#话题:") != std::string::npos || current_data.raw_message.find("#话题：") != std::string::npos)
		{
			auto user = this->user_messages->find(current_data.user_id);
			if (user == this->user_messages->end())
			{
				this->addUsers(current_data.user_id);
			}
			current_data.raw_message = current_data.raw_message.substr(current_data.raw_message.find("#话题") + 7); // 截断
			user->second.user_chatHistory[0].first = (this->users_message_format + JsonParse::getInstance().toJson(current_data.raw_message) + "\"},");
			user->second.user_chatHistory[0].second = time(nullptr);
			user->second.user_chatHistory[1].first = (this->bot_message_format + "OK!I will use Chinses answer \"},");
			user->second.user_chatHistory[1].second = time(nullptr);

			current_data.raw_message = "好的，接下来我会围绕此话题进行对话";
			std::lock_guard<std::mutex> lock(mutex_message);
			this->user_messages->find(current_data.user_id)->second = user->second;
		}
		else if (current_data.raw_message.compare("#当前气温") == 0)
		{
		}
		else if (current_data.raw_message.compare("#设置定时") == 0)
		{
			current_data.raw_message = TimingTast::getInstance().setFixedRemind(current_data.raw_message, current_data.user_id);
		}
		else if (current_data.raw_message.find("#切换模型") != std::string::npos || current_data.raw_message.find("#模型切换") != std::string::npos)
		{
			current_data.raw_message = this->switchModel(current_data.raw_message, current_data.user_id);
		}
		else if (current_data.raw_message.find("#查询当前模型") != std::string::npos)
		{
			current_data.raw_message = "你当前的模型为：" + this->user_messages->find(current_data.user_id)->second.user_models.first;
		}
		else if (current_data.raw_message.find("#开启语音") != std::string::npos)
		{
			if (!this->global_Voice)
			{
				current_data.raw_message = "管理员临时关闭了该功能，可能是在维护...";
			}
			else
			{
				this->user_messages->find(current_data.user_id)->second.isOpenVoiceMode = true;
				current_data.raw_message = "已开启！";
			}
		}
		else if (current_data.raw_message.find("#关闭语音") != std::string::npos)
		{
			this->user_messages->find(current_data.user_id)->second.isOpenVoiceMode = false;
			current_data.raw_message = "已关闭！";
		}
		else if (current_data.raw_message.find("#生成图片：") != std::string::npos || current_data.raw_message.find("#生成图片:") != std::string::npos)
		{
			current_data.type = "CQ";
			this->provideImageCreation(current_data.user_id, current_data.raw_message);
		}
		else if (current_data.raw_message.find("#删除上条对话") != std::string::npos)
		{
			current_data.raw_message = this->removePreviousContext(current_data.user_id);
		}
		// else if (message.substr(1, 12).find(message.find("#SD绘图")) != message.npos)
		else if (current_data.raw_message.find("#SD绘图") != std::string::npos)
		{
			current_data.type = "CQ";
			this->SDImageCreation(current_data.raw_message);
		}
		else if (current_data.raw_message.compare("#模型列表") == 0)
		{
			current_data.raw_message = "当前支持如下模型：\n";
			if (chatModels.size() < 1)
			{
				current_data.raw_message = "未配置模型";
			}
			// 遍历模型
			for (const auto &pair : chatModels)
			{
				for (const auto &name : pair.first)
				{
					current_data.raw_message.append("\n" + name + "\n");
				}
			}
		}
		else
		{
			if (this->global_Voice && this->user_messages->find(current_data.user_id)->second.isOpenVoiceMode)
			{
				current_data.type = "CQ";
			}
			current_data.raw_message = characterMessage(current_data);
		}
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
	}
}

bool Message::messageFilter(std::string message_type, std::string message)
{
	// 过滤策略
	if (message_type.size() < 4)
	{
		return false;
	}

	if (!strcmp(message_type.c_str(), "group"))
	{
		if (ConfigManager::getInstance().configVariable("OPEN_GROUPCHAT_MESSAGE") == "false")
		{
			return false; // 群消息是否开启
		}

		if (message.find("CQ:at") == message.npos || message.find(ConfigManager::getInstance().configVariable("BOT_QQ")) == message.npos)
		{
			return false; // 过滤非AT消息
		}
	}
	// 检查CQ码，不对转发内容进行
	return true;
}

std::string Message::removeGroupCQCode(const std::string &message)
{
	// 为了防止CQ码和普通文本出现冲突，这里选择半硬编码查找，减少冲突，但不一定彻底解决
	std::string res = message;
	std::string specificCQCode = "[CQ:at,qq=" + ConfigManager::getInstance().configVariable("BOT_QQ");
	int begin = res.find(specificCQCode);
	if (begin == res.npos)
	{
		LOG_WARNING("该消息CQ码格式异常或者不存在CQ码!\n源消息:" + message);
		return res;
	}
	int end = res.find("]", begin);
	// 移除CQ码
	res.erase(begin, end - begin + 2);

	return res;
}

/*此重载供cq函数使用*/
template <typename T>
void recursion(std::stringstream &cq, int &count, T arg)
{
	cq << arg << "]";
}

template <typename First, typename... Args>
void recursion(std::stringstream &cq, int &count, First first_arg, Args... args)
{
	cq << first_arg;
	if (count <= 0)
	{
		cq << "=";
		count++;
	}
	else
	{
		cq << ",";
		count = 0;
	}
	recursion(cq, count, args...); // 递归
}

// CQ码
template <typename First, typename... Args>
std::string CQCode(First first_args, Args... args)
{
	// 获取传入的参数个数
	int count = 0;
	std::stringstream cq;
	cq << "[CQ:" << first_args << ",";
	recursion(cq, count, args...);

	return cq.str();
}

void Message::questPictureID(std::string &message)
{
	/* 临时停用
	std::uniform_int_distribution<int> dist(0, Database::getInstance()->CIU.getSize() - 1);
	message = CQCode("image", "file", Database::getInstance()->CIU.getCURL(dist(mt_rand)));
	*/
}

std::string Message::characterMessage(const JsonData &data)
{
	std::string choices_message_content;

	// 获取用户当前使用的模型
	// auto models = this->user_messages->find(data.user_id)->second.user_models;

	// 是否开启上下文  	当上下文模式为开启状态 || 访问者是管理员时，启用上下文
	if (this->accessibility_chat || data.user_id == std::stoll(ConfigManager::getInstance().configVariable("MANAGER_QQ")))
	{
		int contextMax = 0;

		// 设置模型最大的上下文
		contextMax = std::stoll(ConfigManager::getInstance().configVariable("CONTEXT_MAX"));

		// 提取用户聊天记录
		std::vector<std::pair<std::string, time_t>> user_vector;
		{
			std::lock_guard<std::mutex> lock(mutex_message);
			user_vector = this->user_messages->find(data.user_id)->second.user_chatHistory;
		}

		// 判断消息存活时间
		if (user_vector.back().second + std::stoll(ConfigManager::getInstance().configVariable("MESSAGE_SURVIVAL_TIME")) < time(nullptr))
		{
			user_vector.erase(user_vector.begin() + 2, user_vector.end());
			LOG_WARNING("该用户的消息存活时间大于指定时间，已清空...");
		}
		else // 判断消息是否达到最大token限度
		{
			int c_size = 0; // 这里存储的是占用的字节数

			// 这部分可以优化，现在为了进度先暂时搁置
			c_size = user_vector.begin()->first.size() + (user_vector.begin() + 1)->first.size(); // 统计首部长度
			for (auto it = user_vector.begin() + this->default_message_line; it != user_vector.end(); it++)
			{
				c_size += it->first.size();
				// 删除早期聊天记录
				if (c_size >= contextMax - 512 && user_vector.size() > 4) //  预留512 token，保证判断正常 并且对话超过一轮
				{
					LOG_WARNING("The message reached the maximum tokens limit: " + std::to_string(c_size));
					// 删除的范围
					user_vector.erase(user_vector.begin() + this->default_message_line, it + 1);
					std::cout << "Chat message delete over!" << std::endl;
					std::lock_guard<std::mutex> lock(mutex_message);
					this->user_messages->find(data.user_id)->second.user_chatHistory = user_vector;
					break;
				}
			}
		}

		// 将当前聊天内容添加到上下文（临时存储）
		std::string content = JsonParse::getInstance().toJson(data.raw_message);
		std::string format = this->users_message_format + content + "\"}";
		user_vector.push_back(make_pair(format, time(nullptr)));

		content = "";
		// 拼接上下文
		for (std::vector<std::pair<std::string, time_t>>::const_iterator it = user_vector.begin(); it != user_vector.end(); it++)
		{
			content += it->first;
		}
		content.insert(0, "[");
		content.insert(content.size(), "]");
		std::cout << content << std::endl;

		// 将内容发送至模型
		std::cout << "Send to model..." << std::endl;
		auto response = this->dock->RequestChat(content, &this->user_messages->find(data.user_id)->second);
		if (response.code == 200)
		{
			choices_message_content = response.choices_message_content;
			if (choices_message_content.empty())
			{
				choices_message_content = "系统提示：模型无返回内容！";
				return choices_message_content;
			}
			else
			{
				user_vector.back().first.push_back(',');							  // 格式调整
				format = this->bot_message_format + choices_message_content + "\"},"; // 数据格式化
				user_vector.push_back(make_pair(format, time(nullptr)));			  // 保存结果

				// 判断数据是否超出额定值
				int c_size = 0;

				// 这部分可以优化，现在为了进度先暂时搁置
				c_size = user_vector.begin()->first.size() + (user_vector.begin() + 1)->first.size(); // 提前统计首部长度
				for (auto it = user_vector.end() - 1; it > user_vector.begin() + 1; it--)
				{
					c_size += it->first.size();
					// 删除早期聊天记录
					if ((c_size / 3) >= contextMax - 512) //  预留512 token，保证判断正常
					{
						LOG_WARNING("Chat message delete over!");
						LOG_DEBUG(__LINE__ + "使用了erase");
						user_vector.erase(user_vector.begin() + this->default_message_line, it + 2);
						std::lock_guard<std::mutex> lock(mutex_message);
						this->user_messages->find(data.user_id)->second.user_chatHistory = user_vector;
						break;
					}
				}

				// 确认无误，聊天记录更新
				std::lock_guard<std::mutex> lock(mutex_message);
				this->user_messages->find(data.user_id)->second.user_chatHistory = user_vector;

				// 针对reason状态做处理
				if (response.choices_finish_reason == "length") // 模型max_tokens达到最大长度限制
				{
					LOG_WARNING("该模型的回答长度超出了管理员设定的最大限制...");
					choices_message_content += "\n模型已超出最大长度限制，回答可能不全。";
				}
				else if (response.choices_finish_reason == "content_filter") // 违反AI道德规范
				{
					LOG_WARNING("该模型的回答内容被AI morality filter拦截...");
					choices_message_content += "\n模型返回内容被AI morality filter拦截...";
				}
			}
		}
		else
		{
			LOG_ERROR("OpenAI response error: " + std::to_string(response.code));
			choices_message_content = "系统提示：模型无返回内容！";
			return choices_message_content;
		}
	}
	else
	{
		// 不开启上下文
		LOG_INFO("当前聊天不支持上下文模式...");
		std::string content = JsonParse::getInstance().toJson(data.raw_message);
		content = this->users_message_format + content + "\"}";
		content.insert(0, "[\n");
		content.insert(content.size(), "]");
		std::cout << "send to model..." << std::endl;
		auto response = this->dock->RequestChat(content, &this->user_messages->find(data.user_id)->second);

		std::string choices_message_content;
		if (response.code == 200)
		{
			choices_message_content = response.choices_message_content;
			if (choices_message_content.empty())
			{
				choices_message_content = "系统提示：模型无返回内容！";
				return choices_message_content;
			}
			else
			{
				// 针对reason状态做处理
				if (response.choices_finish_reason == "length") // 模型max_tokens达到最大长度限制
				{
					LOG_WARNING("该模型的回答长度超出了管理员设定的最大限制...");
					choices_message_content += "\n模型已超出最大长度限制，回答可能不全。";
				}
				else if (response.choices_finish_reason == "content_filter") // 违反AI道德规范
				{
					LOG_WARNING("该模型的回答内容被AI morality filter拦截...");
					choices_message_content += "\n模型返回内容被AI morality filter拦截...";
				}
			}
		}
		else
		{
			LOG_ERROR("OpenAI response error: " + std::to_string(response.code));
			choices_message_content = "系统提示：模型无返回内容！";
			return choices_message_content;
		}
	}

	// 打印回复内容
	std::cout << "\033[32m"
			  << "OpenAI response: "
			  << "\033[0m" << choices_message_content << std::endl;

	// 判断是否需要提供文本转语音
	if (this->global_Voice && this->user_messages->find(data.user_id)->second.isOpenVoiceMode)
	{
		// LOG_INFO(std::to_string(data.user_id) + "需要使用文本转语音...");
		// current_data.type = "CQ";
		choices_message_content = this->textToVoice(choices_message_content);
	}
	return choices_message_content;
}

std::string Message::musicShareMessage(const std::string &message, short platform)
{
	// 提取歌手或歌曲
	auto result = message.find(":");
	if (result != std::string::npos)
	{
		result += 1;
	}
	else
	{
		result = message.find("：") + 3;
	}
	std::string musicName = message.substr(result);

	CloudMusicID cm;
	int num = 0;
	uint64_t songID = 0;

	switch (platform)
	{
	case 1:
	{
		std::string res = cm.searchSong(musicName);
		try
		{
			songID = std::stoll(res);
		}
		catch (const std::exception &e)
		{
			std::cerr << "捕获异常：" << e.what() << '\n';
		}
		return CQCode("music", "type", "163", "id", res);
	}
	default:
		return {};
	}
}

void Message::facePackageMessage(std::string &message)
{
	message.clear();
	int num = rand() % Database::getInstance()->imgURL.getIMGURL_size() + 1; // 表情包ID
	message.insert(0, CQCode("image", "file", Database::getInstance()->imgURL.getIMG_URL(num)));
}

std::string Message::atUserMassage(std::string message, uint64_t user_id)
{
	message.insert(0, CQCode("at", "qq", user_id));
	return message;
}

void Message::atAllMessage(std::string &message)
{
	message = CQCode("at", "all");
}

std::tuple<bool, std::string> Message::setPersonality(const std::string &roleName, uint64_t user_id)
{
	// 判断用户是否存在
	if (this->user_messages->find(user_id) == this->user_messages->begin())
	{
		this->addUsers(user_id);
	}

	std::string response;
	bool success;

	if (roleName.find("人格还原") != roleName.npos)
	{
		this->mutex_message.lock();
		auto user = this->user_messages->find(user_id);
		this->mutex_message.unlock();

		user->second.user_chatHistory[0].first = this->system_message_format + this->default_personality;
		user->second.temperature = ConfigManager::getInstance().configVariable("temperature");
		user->second.top_p = ConfigManager::getInstance().configVariable("top_p");
		user->second.frequency_penalty = ConfigManager::getInstance().configVariable("frequency_penalty");
		user->second.presence_penalty = ConfigManager::getInstance().configVariable("presence_penalty");

		// 同步
		std::lock_guard<std::mutex> lock(this->mutex_message);
		this->user_messages->find(user_id) = user;

		response = "人格已还原";
		success = true;
		return {success, response};
	}

	// 提取人格名称
	auto result = roleName.find(":");
	if (result != std::string::npos)
	{
		result += 1;
	}
	else
	{
		result = roleName.find("：") + 3;
	}
	std::string name = roleName.substr(result);

	// 读取人格文件
	std::string path = ConfigManager::getInstance().configVariable("PERSONALITY_PATH") + name + ".txt";
	std::ifstream ifs;
#if defined(__WIN32) || defined(__WIN64)
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter; // 创建宽字符转换器
	std::wstring wpath = converter.from_bytes(path);				  // string转wstring
	std::filesystem::path filePath = wpath;
	ifs.open(filePath);
#else
	ifs.open(path);
#endif
	if (!ifs.is_open())
	{
		LOG_WARNING("文件打开失败，文件路径:" + path);
		response = "系统提示：“" + name + "”人格不存在！";
		success = false;
		return {success, response};
	}
	std::stringstream *buffer = new std::stringstream;
	*buffer << ifs.rdbuf();
	ifs.close();

	// 提取数据
	std::string *originData = new std::string(buffer->str());
	delete buffer;

	std::string personality;
	std::string temperature;
	std::string top_p;
	std::string frequency_penalty;
	std::string presence_penalty;
	size_t begin = 0;
	size_t range = 0;

	auto myLambda = [&]() -> bool
	{
		if (begin == std::string::npos || range < 1)
		{
			LOG_ERROR("配置文件有误！");
			response = "系统提示：“" + name + "”人格未找到！";
			success = false;
			return false;
		}
		return true;
	};

	begin = originData->find("Personlity") + 12;
	range = originData->find("}") - begin;
	if (!myLambda())
	{
	}

	personality = originData->substr(begin, range);
	personality = JsonParse::getInstance().toJson(personality);
	originData->erase(0, originData->find("}") + 1);

	begin = originData->find("Temperature") + 15;
	range = originData->find("}") - begin;
	if (!myLambda())
	{
		return {success, response};
	}

	temperature = originData->substr(begin, range);
	originData->erase(0, originData->find("}") + 1);

	begin = originData->find("Top_p") + 7;
	range = originData->find("}") - begin;
	if (!myLambda())
	{
		return {success, response};
	}

	top_p = originData->substr(begin, range);
	originData->erase(0, originData->find("}") + 1);

	begin = originData->find("Frequency_penalty") + 19;
	range = originData->find("}") - begin;
	if (!myLambda())
	{
		return {success, response};
	}

	frequency_penalty = originData->substr(begin, range);
	originData->erase(0, originData->find("}") + 1);

	begin = originData->find("Presence_penalty") + 18;
	range = originData->find("}") - begin;
	if (!myLambda())
	{
		return {success, response};
	}

	presence_penalty = originData->substr(begin, range);
	originData->erase(0, originData->find("}") + 1);

	delete originData;

	// 超参数检查
	try
	{
		auto Lambda = [&](std::string &Hyperparameter)
		{
			LOG_WARNING("超参数设置有误！将自动调整为0。错误的参数：" + Hyperparameter);
			Hyperparameter = "0";
		};

		if (std::stof(temperature) < 0.0 || std::stof(temperature) > 2.0)
		{
			Lambda(temperature);
		}
		if (std::stof(frequency_penalty) < -2.0 || std::stof(frequency_penalty) > 2.0)
		{
			Lambda(frequency_penalty);
		}
		if (std::stof(presence_penalty) < -2.0 || std::stof(presence_penalty) > 2.0)
		{
			Lambda(presence_penalty);
		}
		if (std::stof(top_p) < 0.0 || std::stof(top_p) > 1.0)
		{
			Lambda(top_p);
		}
	}
	catch (const std::exception &e)
	{
		std::cout << e.what() << std::endl;
		LOG_DEBUG("超参数异常！");
		temperature = "0";
		presence_penalty = "0";
		frequency_penalty = "0";
		top_p = "0";
	}

	// 参数更新
	this->mutex_message.lock();
	auto user = this->user_messages->find(user_id);
	this->mutex_message.unlock();
	user->second.user_chatHistory[0].first = this->system_message_format + personality + "\"},";
	user->second.temperature = temperature;
	user->second.top_p = top_p;
	user->second.frequency_penalty = frequency_penalty;
	user->second.presence_penalty = presence_penalty;

	// 同步
	std::lock_guard<std::mutex> lock(mutex_message);
	this->user_messages->find(user_id) = user;

	response = "启用“" + name + "”人格。";
	success = true;
	return {success, response};
}

std::tuple<bool, std::string> Message::setPersonality(const std::string &roleName, const uint64_t user_id, int tag)
{
	// 判断用户是否存在
	if (this->user_messages->find(user_id) == this->user_messages->begin())
	{
		this->addUsers(user_id);
	}

	bool success = false;
	std::string response;
	std::string name; // 人格名称

	if (this->LightweightPersonalityList.size() < 1)
	{
		LOG_ERROR("轻量型人格数据库为空！");
		response = "设置失败！";
		success = false;
		return {success, response};
	}

	bool flog = true;
	for (auto it = this->LightweightPersonalityList.begin(); it != LightweightPersonalityList.end(); it++)
	{
		if (roleName.find(it->first) != roleName.npos)
		{
			name = it->second;
			flog = false;
			break;
		}
	}

	if (flog)
	{
		LOG_WARNING("人格参数未空或不为内置参数");
		response = "系统提示：未找到此人格！";
		success = false;
		return {success, response};
	}

	std::lock_guard<std::mutex> lock(mutex_message);
	this->user_messages->find(user_id)->second.user_chatHistory[0].first = system_message_format + roleName;
	response = "设置成功";
	success = true;
	return {success, response};
}

std::string Message::resetChat(const uint64_t user_id)
{
	auto user = this->user_messages->find(user_id);
	if (user == this->user_messages->end())
	{
		this->addUsers(user_id);
	}
	else if (user->second.user_chatHistory.size() > 2)
	{
		// 重置对话会删除之前的所有信息，但不包括人格信息
		user->second.user_chatHistory.erase(user->second.user_chatHistory.begin() + 2, user->second.user_chatHistory.end());
		std::lock_guard<std::mutex>
			lock(mutex_message);
		this->user_messages->find(user_id)->second = user->second;
	}
	return "会话重置完成！";
}

std::tuple<bool, std::string> Message::adminTerminal(const std::string &message, uint64_t user_id)
{
	std::string str;
	if (message.find("#开启无障碍聊天") != message.npos)
	{
		this->accessibility_chat = true;
		str = "无障碍聊天已开启！";
	}
	else if (message.find("#关闭无障碍聊天") != message.npos)
	{
		this->accessibility_chat = false;
		str = "无障碍聊天已关闭！";
	}
	else if (message.find("#刷新配置文件") != message.npos)
	{
		ConfigManager::getInstance().refreshConfiguation();
		this->readModelName();
		str = "配置文件已刷新";
	}
	else if (message.find("#激活语音") != message.npos)
	{
		this->global_Voice = true;
		str = "已激活！";
	}
	else if (message.find("#冻结语音") != message.npos)
	{
		this->global_Voice = false;
		str = "已冻结！";
	}
	else if (message.find("#获取服务器inet4") != message.npos)
	{
		str = this->PCStatus->getInet4();
	}
	else if (message.find("#获取服务器inet6") != message.npos)
	{
		str = this->PCStatus->getInet6();
	}
	else if (message.find("#获取服务器公网IP") != message.npos)
	{
		str = this->PCStatus->getPublicIP();
	}
	bool success = !str.empty();
	std::string message_ = std::string(success ? message : str);
	return {success, message_};
}

std::string Message::switchModel(const std::string &message, const uint64_t user_id)
{
	if (message.size() < 14)
	{
		LOG_WARNING("未指定模型...");
		return "系统提示：请选择要切换的模型！";
	}

	// 模型名称提取
	std::string model;
	if (message.find(":") != std::string::npos)
	{
		model = message.substr(message.find(":") + 1);
	}
	else
	{
		model = message.substr(message.find("：") + 3);
	}
	std::string modelName = message;

	// 消除后缀空格
	while (modelName.back() == ' ')
	{
		modelName.erase(modelName.size() - 1, 1);
	}

	// 寻找相同的模型名称
	std::pair<std::string, std::vector<std::string>> newModel;
	for (auto &entry : this->chatModels)
	{
		if (entry.first.find(modelName) != entry.first.end())
		{
			newModel.first = modelName;
			newModel.second.push_back(entry.second[0]);
			newModel.second.push_back(entry.second[1]);
			newModel.second.push_back(entry.second[2]);
			this->user_messages->find(user_id)->second.user_models = newModel;
			return std::string("设置成功，当前模型为:" + this->user_messages->find(user_id)->second.user_models.first);
		}
	}
	/*
	for (auto GPTModel = this->chatModels.begin(); GPTModel != this->chatModels.end(); GPTModel++)
	{
		if (GPTModel->first == modelName)
		{
			this->user_messages->find(user_id)->second.user_models = *GPTModel;
			message = "设置成功，当前模型为:" + this->user_messages->find(user_id)->second.user_models.first;
			return;
		}
	}
	*/
	return "系统提示：不存在的模型!";
}

bool Message::permissionVerification(uint64_t user_id)
{
	return user_id == std::stoll(ConfigManager::getInstance().configVariable("MANAGER_QQ")) ? true : false;
}

// 回调函数用于写入数据到文件
size_t write_data(void *ptr, size_t size, size_t nmemb, std::string *data)
{
	data->append(reinterpret_cast<const char *>(ptr), size * nmemb);
	return size * nmemb;
}

void Message::call_fixImageSizeTo4K(std::string &message)
{
	Realesrgan *rlg = new Realesrgan;
	std::string imagePath = rlg->fixImageSizeTo4K(message);

	if (imagePath.empty())
	{
		LOG_ERROR("返回内容少于20字节");
		return;
	}
	else
	{
		// 路径传输
		message.insert(0, "file://");
		message = CQCode("image", "file", message);
	}
	// else if (res == 2)
	// {
	// 	// base64传输
	// 	message = CQCode("image", "file=base64://" + message);
	// }

	delete rlg;
}

// 数据流转为base64编码
std::string Message::dataToBase64(const std::string &input)
{
	const std::string base64_chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	std::string encoded;
	int val = 0;
	int bits = -6;
	const unsigned int mask = 0x3F; // 0b00111111

	for (unsigned char c : input)
	{
		val = (val << 8) + c;
		bits += 8;
		while (bits >= 0)
		{
			encoded.push_back(base64_chars[(val >> bits) & mask]);
			bits -= 6;
		}
	}

	if (bits > -6)
	{
		encoded.push_back(base64_chars[((val << 8) >> (bits + 8)) & mask]);
	}

	while (encoded.size() % 4 != 0)
	{
		encoded.push_back('=');
	}

	return encoded;
}

std::string Message::encodeToURL(const std::string &input)
{
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex;

	for (char c : input)
	{
		// 保持字母数字和其他可接受的字符不变
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			escaped << c;
		}
		else if (c == ' ')
		{
			escaped << '+';
		}
		else
		{
			escaped << std::uppercase;
			escaped << '%' << std::setw(2) << int((unsigned char)c);
			escaped << std::nouppercase;
		}
	}
	return escaped.str();
}

std::string Message::provideImageRecognition(const uint64_t user_id, const std::string &message, const std::string &message_data_url)
{
	std::string conversation = message.substr(0, message.find("[CQ:image")); // 提取对话（如果有的话）

	// 若不存在prompt，则设置默认prompt
	if (conversation.size() < 6)
	{
		conversation = "Please analyze this picture in all aspects and answer it in Chinese";
	}
	conversation = JsonParse::getInstance().toJson(conversation);

	// 初始化
	CURL *curl_handle = curl_easy_init();
	if (!curl_handle)
	{
		LOG_ERROR("curl 无法初始化。");
		return {};
	}
	CURLcode res;
	std::string input;

	// 开始执行下载操作
	if (curl_handle)
	{
#if defined(__WIN32) || defined(__WIN64)
		// 设置SSL证书验证
		curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 1L);	 // 开启SSL证书验证
		curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L);	 // 验证证书中的主机名
		curl_easy_setopt(curl_handle, CURLOPT_CAINFO, "cacert.pem"); // 指定CA根证书
#endif
		curl_easy_setopt(curl_handle, CURLOPT_URL, message_data_url.c_str());
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &input);

		res = curl_easy_perform(curl_handle);
		if (res != CURLE_OK)
		{
			LOG_ERROR("Failed to download image: " + std::string(curl_easy_strerror(res)));
			curl_easy_cleanup(curl_handle);
			return {};
		}
	}
	else
	{
		LOG_ERROR("Failed to initialize curl handle.");
		return {};
	}
	curl_easy_cleanup(curl_handle);
	LOG_INFO("图片下载完成，大小为：" + std::to_string(input.size() / 1024.0) + "KB");

	// 下载完成，将数据转为base64编码
	std::string base64 = this->dataToBase64(input);

	// 封装消息，向OpenAI发送  这里可以检查收到的信息是否合法text
	std::cout << "send to vision model..." << std::endl;
	std::string endpoint = ConfigManager::getInstance().configVariable("VISION_MODEL_ENDPOINT");
	std::string api_key = ConfigManager::getInstance().configVariable("VISION_MODEL_API_KEY");
	std::string model = ConfigManager::getInstance().configVariable("VISION_MODEL");

	auto response = this->dock->RequestVision(endpoint, api_key, model, conversation, base64);

	std::string answer = response.choice_message_content;
	std::cout << "OpenAI response: " << answer << std::endl;

	if (response.code == 200)
	{
		if (response.finish_reason == "length")
		{
			answer += "\n系统提示：返回的内容超过管理员设定的最大长度。";
		}
		else if (response.finish_reason == "content_filter")
		{
			answer = response.choice_message_refusal;
		}

		// 判断是否需要转语音
		if (this->user_messages->find(user_id)->second.isOpenVoiceMode)
		{
			answer = this->textToVoice(answer);
			if (answer.find(".wav") == std::string::npos)
			{
				answer = "系统提示：语音模块异常。";
			}
		}
		// return CQCode("reply", "message", answer);
		return answer;
	}
	else
	{
		LOG_ERROR("OpenAI response: " + response.choice_message_content);
		answer = "系统提示：分析超时，请重试。";
	}

	return {};
}

// 专供 textToVoice 函数的回调函数
size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	std::ofstream *file = static_cast<std::ofstream *>(userp);
	if (file)
	{
		file->write(static_cast<const char *>(contents), realsize);
	}
	return realsize;
}
std::string Message::textToVoice(const std::string &text)
{
	std::string response;

	// LOG_DEBUG("转语音的内容：" + text);
	std::string audioPath = this->voice->toAudio(JsonParse::getInstance().toJson(text));
	if (audioPath.find(".wav") != std::string::npos)
	{
		// 使用路径传输
		std::string URL = "file://" + text;
		response = CQCode("record", "file", URL);
		return response;
	}
	else
	{
		return audioPath; // 这里面是错误信息
	}
}

std::string Message::provideImageCreation(const uint64_t user_id, const std::string &text)
{
	// 提取prompt
	std::string prompt;
	if (text.find(":") != text.npos)
	{
		prompt = text.substr(text.find(":") + 1);
	}
	else
	{
		prompt = text.substr(text.find("：") + 3); // 在Linux中，3个位置放一个中文字符
	}

	if (prompt.size() < 3)
	{
		return "系统提示：描述过短！至少存在1个汉字或者3个字符...";
	}

	// 开始请求OpenAI
	std::cout << "send to Model..." << std::endl;
	prompt = JsonParse::getInstance().toJson(prompt);
	std::pair<std::string, std::vector<std::string>> p;
	p.first = ConfigManager::getInstance().configVariable("DRAW_MODEL");
	p.second.push_back(ConfigManager::getInstance().configVariable("DRAW_MODEL_API_KEY"));
	p.second.push_back(ConfigManager::getInstance().configVariable("DRAW_MODEL_ENDPOINT"));
	p.second.push_back(ConfigManager::getInstance().configVariable("DRAW_MODEL_APISTANDARD"));
	auto result = this->user_messages->find(user_id)->second; // 值拷贝
	result.user_models = p;
	auto response = this->dock->RequestDraw(prompt);

	if (response.code >= 400)
	{
		return "网络异常...";
	}
	std::string URL = response.data_base64;
	return CQCode("image", "base64", URL);
}

std::string Message::removePreviousContext(const uint64_t user_id)
{
	std::vector<std::pair<std::string, time_t>> user_context;
	{
		std::lock_guard<std::mutex> lock(mutex_message);
		user_context = this->user_messages->find(user_id)->second.user_chatHistory;
	}

	if (user_context.size() < 3)
	{
		return std::string("没有上下文！");
	}

	// 删除最近的上下文
	user_context.pop_back();
	user_context.pop_back();

	// 跟具体数据同步
	std::lock_guard<std::mutex> lock(mutex_message);
	this->user_messages->find(user_id)->second.user_chatHistory = user_context;

	return std::string("上条对话已被删除！");
}

std::string Message::SDImageCreation(const std::string &message)
{
	std::string response;
	// 提取出prompt
	if (message.size() < 14)
	{
		LOG_WARNING("用户未描述图像");
		return "系统提示：请描述图像...";
	}

	std::string prompt;
	if (message.find(":") != message.npos)
	{
		prompt = message.substr(message.find(":") + 1);
	}
	else
	{
		prompt = message.substr(message.find("：") + 3); // 在Linux中，3个位置放一个中文字符
	}

	// prompt翻译成英文

	// 调用StableDiffusion
	std::string base64_code;

	std::unique_ptr<StableDiffusion> SD = std::make_unique<StableDiffusion>();

	// StableDiffusion &SD =
	base64_code = SD->connectStableDiffusion(prompt);

	if (base64_code.size() < 128)
	{
		return "系统提示：数据返回有误！";
	}
	response = "[CQ:image,file=base64://";
	response.append(base64_code);
	response.append("]");

	return response;
}

void Message::readModelName()
{
	// 轻量型人格初始化
#ifdef DEBUG
	LOG_DEBUG("模型文件格初始化...");
#endif
	// 载入模型名称
	std::ifstream ifsJson(ConfigManager::getInstance().configVariable("CHATMODELS_PATH"));
	if (!ifsJson.is_open())
	{
		LOG_ERROR("模型配置文件打开失败！请检查该文件是否存在。");
	}
	std::string json((std::istreambuf_iterator<char>(ifsJson)), std::istreambuf_iterator<char>());
	nlohmann::json document;
	try
	{
		document = nlohmann::json::parse(json);
		if (!document.contains("Models") || !document["Models"].is_array())
		{
			LOG_FATAL("JSON 数据中缺少 Models 字段或 Models 不是数组！");
			return;
		}

		// 遍历 Models 数组
		this->chatModels.clear();
		const nlohmann::json &models = document["Models"];
		for (int i = 0; i < models.size(); i++)
		{
			const nlohmann::json &model = models[i];
			std::unordered_set<std::string> modelNames;
			std::vector<std::string> otherInfo(3, ""); // 初始化为 3 个空字符串

			// 提取 ModelName 或 name 字段
			if (model.contains("ModelName") && model["ModelName"].is_array())
			{
				const nlohmann::json &modelNamesArray = model["ModelName"];
				for (int j = 0; j < modelNamesArray.size(); j++)
				{
					modelNames.insert(modelNamesArray[j]);
				}
			}
			else if (model.contains("name") && model["name"].is_array())
			{
				const nlohmann::json &modelNamesArray = model["name"];
				for (int j = 0; j < modelNamesArray.size(); j++)
				{
					modelNames.insert(modelNamesArray[j]);
				}
			}

			// 提取其余字段
			otherInfo[0] = model.value("api_key", "");
			otherInfo[1] = model.value("api_endpoint", "");
			otherInfo[2] = model.value("APIStandard", "");
			chatModels.push_back(std::make_pair(modelNames, otherInfo));
		}
	}
	catch (const std::exception &e)
	{
		LOG_FATAL("Models JSON 解析失败！error:" + std::string(e.what()));
		return;
	}
}

Message::~Message()
{
	if (this->user_messages != nullptr)
	{
		delete this->user_messages;
		this->user_messages = nullptr;
	}
}
