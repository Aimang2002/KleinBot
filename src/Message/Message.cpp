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

Message::Message() : userSession(models)
{
	// 服务器状态类初始化
#ifdef DEBUG
	LOG_DEBUG("服务器状态初始化...");
#endif
	this->PCStatus = std::make_unique<ComputerStatus>();
	this->voice = std::make_unique<Voice>();
	this->dock = std::make_unique<Dock>();
	this->models.reload();
	// 内置成员属性初始化
	this->accessibility_chat = ConfigManager::getInstance().configVariable("ACCESSIBLITY_CHAT") == "true" ? true : false;
	this->global_Voice = ConfigManager::getInstance().configVariable("GLOBAL_VOICE") == "true" ? true : false;

	// 注册所有命令事件
	this->registry.registryCommand(std::make_unique<HelpCommand>());
	this->registry.registryCommand(std::make_unique<ModelListCommand>(this->models));
	this->registry.registryCommand(std::make_unique<SearchSongsCommand>());
	this->registry.registryCommand(std::make_unique<QueryModelCommand>([&](uint64_t uid)
																	   { return this->userSession.getModelName(uid); }));
	this->registry.registryCommand(std::make_unique<GeneratePictureCommand>(*this->dock));
	this->registry.registryCommand(std::make_unique<ResetChatCommand>(this->userSession));
	this->registry.registryCommand(std::make_unique<SetSoulCommand>(this->userSession));
	this->registry.registryCommand(std::make_unique<SwitchModelCommand>(this->userSession, this->models));
	this->registry.registryCommand(std::make_unique<VoiceSwitchCommand>(this->userSession, this->global_Voice));
	this->registry.registryCommand(std::make_unique<RemoveContextCommand>(this->userSession));
	this->registry.registryCommand(std::make_unique<AdminCommand>(*this->PCStatus, this->accessibility_chat, this->global_Voice, [this]()
																  {		ConfigManager::getInstance().refreshConfiguation();
																		this->models.reload(); }));

	srand((unsigned int)time(NULL));

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

Message::Intent Message::classify(const JsonData &data)
{
	if (data.post_type != "message") // 明确为心跳包
	{
		return Intent::SystemEvent;
	}
	else if (!data.message_data_url.empty()) // 如果该上报有数据，就表示用户上传了图片，使用视觉识别
	{
		return Intent::Vision;
	}
	return Intent::Chat; // 正常聊天
}

void Message::handleMessage(JsonData &current_data)
{
	// 基础分类
	Intent intent = classify(current_data);

	// 系统事件（心跳/通知/请求）—— 静默忽略
	if (intent == Intent::SystemEvent)
	{
		return;
	}
	else if (intent == Intent::Chat) // 文本消息：先尝试命令匹配，匹配失败再走聊天
	{
		CommandContext ctx{current_data.user_id, current_data.group_id, current_data.message_type, current_data};
		auto rs = this->registry.execute(current_data.raw_message, ctx);
		if (rs.has_value())
		{
			LOG_DEBUG("识别到命令");
			current_data.raw_message = rs->message;
			current_data.content_type = (rs->type == MessageType::CQ) ? "CQ" : "text";
			return;
		}
	}

	// 普通对话
	try
	{
		current_data.content_type = "text";

		std::cout << "[" << current_data.message_type << "]" << current_data.user_id << ":" << current_data.plain_text << std::endl;

		if (intent == Intent::Vision)
		{
			current_data.raw_message = provideImageRecognition(current_data.user_id, current_data.plain_text, current_data.message_data_url);
		}
		else
		{
			current_data.raw_message = characterMessage(current_data);
		}

		// 语音转换
		if (this->global_Voice && this->userSession.isVoiceMode(current_data.user_id))
		{
			current_data.content_type = "CQ";
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

/*此重载供cq函数使用*/
std::string Message::characterMessage(const JsonData &data)
{
	// 上下文模式开关：全局开启 || 访问者是管理员
	const bool useContext = this->accessibility_chat || data.user_id == std::stoll(ConfigManager::getInstance().configVariable("MANAGER_QQ"));

	// 1. 上下文模式：先把用户这句追加到历史
	if (useContext)
	{
		auto history = this->userSession.getChatHistory(data.user_id);
		history.push_back({"user", data.plain_text, time(nullptr)});
		this->userSession.updateChatHistory(data.user_id, history);
	}

	// 2. 构造请求包（USS 负责裁切、查模型、拼超参数）
	auto bundleOpt = this->userSession.buildChatRequest(data.user_id);
	if (!bundleOpt)
	{
		return "系统提示：当前模型未注册，请管理员检查 ModelsName.json。";
	}
	auto &bundle = *bundleOpt;

	// 非上下文模式：清空历史，只发当前这条
	if (!useContext)
	{
		LOG_INFO("当前聊天不支持上下文模式...");
		bundle.request.history.clear();
		bundle.request.history.push_back({"user", data.plain_text});
	}

	// 3. 调用 LLM
	std::cout << "Send to model..." << std::endl;
	auto response = this->dock->RequestChat(bundle.model, bundle.model_name, bundle.request);

	// 4. 错误处理
	if (response.code != 200)
	{
		LOG_ERROR("LLM response error code: " + std::to_string(response.code));
		return response.error_message.empty() ? std::string("系统提示：模型无返回内容！") : "系统提示：" + response.error_message;
	}

	std::string LLM_content = response.content;
	if (LLM_content.empty())
	{
		return "系统提示：模型无返回内容！";
	}

	// 5. 上下文模式：把回复也写回历史
	if (useContext)
	{
		auto history = this->userSession.getChatHistory(data.user_id);
		history.push_back({"assistant", LLM_content, time(nullptr)});
		this->userSession.updateChatHistory(data.user_id, history);
	}

	// 6. finish_reason 附加提示
	if (response.finish_reason == "length")
	{
		LOG_WARNING("该模型的回答长度超出了管理员设定的最大限制...");
		LLM_content += "\n模型已超出最大长度限制，回答可能不全。";
	}
	else if (response.finish_reason == "content_filter")
	{
		LOG_WARNING("该模型的回答内容被AI morality filter拦截...");
		LLM_content += "\n模型返回内容被AI morality filter拦截...";
	}

	std::cout << "\033[32m" << "Model response: " << "\033[0m" << LLM_content << std::endl;
	return LLM_content;
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

	std::cout << "send to vision model..." << std::endl;

	ChatModel model;
	model.endpoint = ConfigManager::getInstance().configVariable("VISION_MODEL_ENDPOINT");
	model.api_key = ConfigManager::getInstance().configVariable("VISION_MODEL_API_KEY");
	model.api_standard = ConfigManager::getInstance().configVariable("VISION_MODEL_APISTANDARD");
	std::string modelName = ConfigManager::getInstance().configVariable("VISION_MODEL");

	auto response = this->dock->RequestVision(model, modelName, conversation, base64);
	std::string answer = response.content;
	std::cout << "OpenAI response: " << answer << std::endl;
	if (response.code != 200)
	{
		LOG_ERROR("OpenAI response: " + response.content);
		answer = "系统提示：分析超时，请重试。";
	}
	else
	{
		if (response.finish_reason == "length")
		{
			answer += "回复过长，可能是程序bug，请联系管理员上报bug";
		}
		else if (response.finish_reason == "content_filter")
		{
			answer = response.refusal;
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
		return answer;
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

	std::string audioPath = this->voice->toAudio(text);
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

std::string Message::pushScheduled(uint64_t user_id, const std::string &prompt)
{
	std::string modelName = ConfigManager::getInstance().configVariable("DEFAULT_MODEL");

	const ChatModel *modelPtr = this->models.find(modelName);
	if (modelPtr == nullptr)
	{
		LOG_ERROR("DEFAULT_MODEL 未在 ModelsName.json 注册：" + modelName);
		return "系统提示：默认模型未配置";
	}

	ChatRequest request;
	request.frequency_penalty = std::stod(ConfigManager::getInstance().configVariable("frequency_penalty"));
	request.presence_penalty = std::stod(ConfigManager::getInstance().configVariable("presence_penalty"));
	request.temperature = std::stod(ConfigManager::getInstance().configVariable("temperature"));
	request.system_prompt = "你是人工助手";
	request.history.push_back({"user", prompt});

	ChatResponse response = this->dock->RequestChat(*modelPtr, modelName, request);

	if (response.code != 200)
	{
		LOG_ERROR("早安推送失败：" + std::to_string(response.code));
		return response.error_message.empty() ? "网络异常" : response.error_message;
	}
	return response.content;
}

Message::~Message()
{
}
