#include "../ModelApiCaller/StableDiffusion/StableDiffusion.h"
#include "../ModelApiCaller/Realesrgan/Realesrgan.h"
#include "../ModelApiCaller/Dock.hpp"
#include "../submodules/CloudMusicID/CloudMusicID.h"
#include "../Command/HelpCommand.h"
#include "../Command/ModelListCommand.h"
#include "../utils/Utils.hpp"
#include "../Command/SearchSongsCommand.h"
#include "../Command/QueryModelCommand.h"
#include "../Command/GeneratePictureCommand.h"
#include "../Command/ResetChatCommand.h"
#include "../Command/SetSoulCommand.h"
#include "../Command/SwitchModelCommand.h"
#include "../Command/VoiceSwitchCommand.h"
#include "../Command/RemoveContextCommand.h"
#include "../Command/AdminCommand.h"
#include "Message.h"
#include <iomanip>

std::mt19937 mt_rand(1000);

Message::Message()
{
	// 服务器状态类初始化
#ifdef DEBUG
	LOG_DEBUG("服务器状态初始化...");
#endif
	this->PCStatus = std::make_unique<ComputerStatus>();
	this->voice = std::make_unique<Voice>();
	this->dock = std::make_unique<Dock>();

	// 注册所有命令事件
	this->registry.registryCommand(std::make_unique<HelpCommand>());
	this->registry.registryCommand(std::make_unique<ModelListCommand>(this->chatModels));
	this->registry.registryCommand(std::make_unique<SearchSongsCommand>());
	this->registry.registryCommand(std::make_unique<QueryModelCommand>([&](uint64_t uid)
																	   { return this->userSession.getModelName(uid); }));
	this->registry.registryCommand(std::make_unique<GeneratePictureCommand>());
	this->registry.registryCommand(std::make_unique<ResetChatCommand>(this->userSession));
	this->registry.registryCommand(std::make_unique<SetSoulCommand>(this->userSession));
	this->registry.registryCommand(std::make_unique<SwitchModelCommand>(this->userSession, this->chatModels));
	this->registry.registryCommand(std::make_unique<VoiceSwitchCommand>(this->userSession, this->global_Voice));
	this->registry.registryCommand(std::make_unique<RemoveContextCommand>(this->userSession));
	this->registry.registryCommand(std::make_unique<AdminCommand>(*this->PCStatus, this->accessibility_chat, this->global_Voice, [this]()
																  {		ConfigManager::getInstance().refreshConfiguation();
																		this->readModelName(); }));

	srand((unsigned int)time(NULL));

	// 内置成员属性初始化
	this->accessibility_chat = ConfigManager::getInstance().configVariable("ACCESSIBLITY_CHAT") == "true" ? true : false;
	this->global_Voice = ConfigManager::getInstance().configVariable("GLOBAL_VOICE") == "true" ? true : false;

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
	this->userSession.ensureUserExists(manager_qq);
}

void Message::handleMessage(JsonData &current_data)
{
	// 查看是否是命令
	CommandContext ctx{current_data.user_id, current_data.group_id, current_data.message_type, current_data};
	auto rs = this->registry.execute(current_data.raw_message, ctx);
	if (rs.has_value())
	{
		LOG_DEBUG("识别到命令");
		current_data.raw_message = rs->message;
		current_data.type = (rs->type == MessageType::CQ) ? "CQ" : "text";
		return;
	}

	try
	{
		// type类型默认为text，如需要更改需要重新指定
		current_data.type = "text";
		// 判断是否是群消息
		if (current_data.message_type == "group")
		{
			// 移除CQ码
			current_data.raw_message = this->removeGroupCQCode(current_data.raw_message);
		}

		// 显示用户输入的问题
		std::cout << "[" << current_data.message_type << "]" << current_data.user_id << ":" << current_data.raw_message << std::endl;
		current_data.raw_message = characterMessage(current_data);

		// 判断是否需要提供文本转语音
		if (this->global_Voice && this->userSession.isVoiceModel(current_data.user_id))
		{
			current_data.type = "CQ";
			current_data.raw_message = this->textToVoice(current_data.raw_message);
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
std::string Message::characterMessage(const JsonData &data)
{
	std::string choices_message_content;

	// 是否开启上下文  	当上下文模式为开启状态 || 访问者是管理员时，启用上下文
	if (this->accessibility_chat || data.user_id == std::stoll(ConfigManager::getInstance().configVariable("MANAGER_QQ")))
	{
		int contextMax = 0;

		// 设置模型最大的上下文
		contextMax = std::stoll(ConfigManager::getInstance().configVariable("CONTEXT_MAX"));

		// 提取用户聊天记录
		auto user_vector = this->userSession.getChatHistory(data.user_id);

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
			for (auto it = user_vector.begin() + this->userSession.getDefaultLine(); it != user_vector.end(); it++)
			{
				c_size += it->first.size();
				// 删除早期聊天记录
				if (c_size >= contextMax - 512 && user_vector.size() > 4) //  预留512 token，保证判断正常 并且对话超过一轮
				{
					LOG_WARNING("The message reached the maximum tokens limit: " + std::to_string(c_size));
					// 删除的范围
					user_vector.erase(user_vector.begin() + this->userSession.getDefaultLine(), it + 1);
					std::cout << "Chat message delete over!" << std::endl;
					this->userSession.updateChatHistory(data.user_id, user_vector);
					break;
				}
			}
		}

		// 将当前聊天内容添加到上下文（临时存储）
		std::string newContent = data.raw_message;
		std::string format = this->userSession.dumpUserMessage(newContent);
		user_vector.push_back(make_pair(format, time(nullptr)));

		nlohmann::json content = nlohmann::json::array();
		// 拼接上下文
		for (std::vector<std::pair<std::string, time_t>>::const_iterator it = user_vector.begin(); it != user_vector.end(); it++)
		{
			content.push_back(nlohmann::json::parse(it->first));
		}
		std::cout << content.dump(2) << std::endl;

		// 将内容发送至模型
		std::cout << "Send to model..." << std::endl;
		auto response = this->dock->RequestChat(content.dump(), this->userSession.getUserConfig(data.user_id));
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
				format = this->userSession.dumpBotMessage(choices_message_content);
				user_vector.push_back(make_pair(format, time(nullptr))); // 保存结果

				// 判断数据是否超出额定值
				int c_size = 0;

				// 这部分可以优化，现在为了进度先暂时搁置
				c_size = user_vector.begin()->first.size() + (user_vector.begin() + 1)->first.size(); // 提前统计首部长度
				for (auto it = user_vector.end() - 1; it > user_vector.begin() + 1; it--)
				{
					c_size += it->first.size();
					// 删除早期聊天记录
					if ((c_size / 3) >= contextMax - 512)
					{
						LOG_WARNING("Chat message delete over!");
						LOG_DEBUG(std::to_string(__LINE__) + "使用了erase");
						user_vector.erase(user_vector.begin() + this->userSession.getDefaultLine(), it + 2); // 这里有问题，后面需要大改
						this->userSession.updateChatHistory(data.user_id, user_vector);
						break;
					}
				}

				// 确认无误，聊天记录更新
				this->userSession.updateChatHistory(data.user_id, user_vector);

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
			LOG_ERROR("OpenAI response error code: " + std::to_string(response.code));
			if (!response.error_message.empty())
			{
				choices_message_content = "系统提示：" + response.error_message;
			}
			else
			{
				choices_message_content = "系统提示：模型无返回内容！";
			}
			return choices_message_content;
		}
	}
	else
	{
		// 不开启上下文
		LOG_INFO("当前聊天不支持上下文模式...");
		std::string newContent = data.raw_message;
		nlohmann::json content = nlohmann::json::array();
		content.push_back(nlohmann::json::parse(this->userSession.dumpUserMessage(newContent)));

		std::cout << "send to model..." << std::endl;
		auto response = this->dock->RequestChat(content.dump(), this->userSession.getUserConfig(data.user_id));

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
	uint64_t songID = 0;

	switch (platform)
	{
	case 1:
	{
		nlohmann::json res = cm.searchSong(musicName);
		if (res.contains("result") && res["result"].is_object())
		{
			auto r = res["result"];
			if (r.contains("songs") && r["songs"].is_array() && !r["songs"].empty() && r["songs"][0].is_object())
			{
				songID = r["songs"][0].value("id", uint64_t(0));
			}
		}
		return utils::CQCode("music", "type", "163", "id", songID);
	}
	default:
		return {};
	}
	return {};
}

std::string Message::atUserMassage(std::string message, uint64_t user_id)
{
	message.insert(0, utils::CQCode("at", "qq", user_id));
	return message;
}

void Message::atAllMessage(std::string &message)
{
	message = utils::CQCode("at", "all");
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
		message = utils::CQCode("image", "file", message);
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
	LOG_INFO("图片下载完成，大小为：" + std::to_string(input.size() / 1024.0 / 1024.0) + "MB");

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
		if (this->userSession.getUserConfig(user_id).isOpenVoiceMode)
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
		std::string URL = "file://" + audioPath;
		response = utils::CQCode("record", "file", URL);
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
	std::string endpoint = ConfigManager::getInstance().configVariable("DRAW_MODEL_ENDPOINT");
	std::string api_key = ConfigManager::getInstance().configVariable("DRAW_MODEL_API_KEY");
	std::string model = ConfigManager::getInstance().configVariable("DRAW_MODEL");

	auto response = this->dock->RequestDraw(endpoint, api_key, model, prompt);

	if (response.code >= 400)
	{
		return "系统提示：网络异常...";
	}
	std::string base64 = response.data_base64;
	base64 = base64.insert(0, "base64://");
	std::string result = utils::CQCode("image", "file", base64);
	return result;
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
}
