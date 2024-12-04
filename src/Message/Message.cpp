#include "Message.h"

std::mt19937 mt_rand(1000);

Message::Message()
{
	srand((unsigned int)time(NULL));

	// 服务器状态类初始化
#ifdef DEBUG
	LOG_DEBUG("服务器状态初始化...");
#endif
	this->PCStatus = new ComputerStatus;
	this->voice = new Voice;

	// 内置成员属性初始化
	this->user_messages = new std::map<uint64_t, Person>;
	this->accessibility_chat = CManager.configVariable("GLOBAL_VOICE") == "true" ? true : false;
	this->global_Voice = CManager.configVariable("ACCESSIBLITY_CHAT") == "true" ? true : false;
	this->system_message_format = R"({"role": "system", "content": ")";
	this->bot_message_format = R"({"role": "assistant", "content": ")";
	this->users_message_format = R"({"role": "user", "content": ")";
	this->default_personality = "You are my assistant, your name is " + CManager.configVariable("QBOT_NAME") + "\"},";
	this->default_message_line = 2;

// 载入模型名称
#ifdef DEBUG
	LOG_DEBUG("正在载入模型名称...");
#endif
	std::ifstream ifsJson(CManager.configVariable("CHATMODELS_PATH"));
	if (ifsJson.is_open())
	{
		std::string json((std::istreambuf_iterator<char>(ifsJson)), std::istreambuf_iterator<char>());
		Document document;
		if (document.Parse(json.c_str()).HasParseError() && (document.HasMember("Models") != true))
		{
			std::cerr << "JSON parse error: " << GetParseError_En(document.GetParseError()) << std::endl;
			LOG_ERROR("JSON数据解析失败！请检查Json文件的合法性。");
		}
		else
		{
			const rapidjson::Value &models = document["Models"];
			if (models.IsArray())
			{
				for (SizeType i = 0; i < models.Size(); i++)
				{
					const Value &model = models[i];
					if ((model.HasMember("name") && model["name"].IsString()) &&
						(model.HasMember("APIStandard") && model["APIStandard"].IsString()))
					{
						std::pair<std::string, std::string> p;
						p.first = model["name"].GetString();
						p.second = model["APIStandard"].GetString();
						this->chatModels.push_back(p);
					}
				}
			}
		}
	}
	else
	{
		LOG_ERROR("模型配置文件打开失败！请检查该文件是否存在。");
	}

	// 轻量型人格初始化
#ifdef DEBUG
	LOG_DEBUG("轻量型人格初始化...");
#endif
	std::string path = CManager.configVariable("PERSONALITY_PATH") + "personality.txt";
	std::ifstream ifs(path);
	if (!ifs.is_open())
	{
		perror("轻量型人格初始化失败");
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
	this->addUsers(stoi(CManager.configVariable("MANAGER_QQ")));
}

bool Message::addUsers(uint64_t user_id)
{
	if (user_messages->find(user_id) == user_messages->end())
	{
		// 添加默认数据
		std::vector<std::pair<std::string, time_t>> userDefault;
		std::pair<std::string, time_t> p;
		p.first = this->system_message_format + this->default_personality; // 设置system信息
		p.second = time(nullptr);										   // 获取当前时间
		userDefault.push_back(p);

		p.first = this->bot_message_format + "OK!I will use Chinses answer\"},"; // 设置assistant信息
		userDefault.push_back(p);

		// 创建用户
		Person person;
		person.user_chatHistory = userDefault;

		std::pair<std::string, std::string> models;
		models.first = CManager.configVariable("DEFAULT_MODEL"); // 默认模型
		models.second = CManager.configVariable("DEFAULT_MODEL_APISTANDARD");
		person.user_models = models;

		person.isOpenVoiceMode = false;
		person.temperature = CManager.configVariable("temperature");
		person.top_p = CManager.configVariable("top_p");
		person.frequency_penalty = CManager.configVariable("frequency_penalty");
		person.presence_penalty = CManager.configVariable("presence_penalty");

		std::lock_guard<std::mutex> lock(mutex_message);
		this->user_messages->insert(std::make_pair(user_id, person));
		return true;
	}
	return false;
}

// string Message::handleMessage(uint64_t user_id, string message, string message_type)
std::string Message::handleMessage(JsonData &data)
{
	// type类型默认为text，如需要更改需要重新指定
	data.type = "text";

	// 新建用户
	if (this->user_messages->find(data.private_id) == this->user_messages->end())
	{
		this->addUsers(data.private_id);
	}

	// 判断是否是群消息
	if (data.message_type == "group")
	{
		// 移除CQ码
		this->removeGroupCQCode(data.message);
	}

	// 显示用户输入的问题
	std::cout << "[" << data.message_type << "]" << data.private_id << ":" << data.message << std::endl;

	// 管理员权限判断
	if (this->permissionVerification(data.private_id))
	{
		// 管理员可操作的命令
		bool result = this->adminTerminal(data.message, data.private_id);
		if (result)
		{
			return data.message;
		}
	}

	// 内置回复判断

	if (data.message.find("#图片超分") != data.message.npos)
	{
		this->call_fixImageSizeTo4K(data.message);
		data.type = "CQ";
	}
	/*
	else if (message.find("#精美图片") != message.npos)
	{
		questPictureID(message);
		LOG_DEBUG(message);
	}
	*/
	else if (data.message == CManager.configVariable("QBOT_NAME"))
	{
		SpeechSound(data.message);
	}
	else if (data.message.find("CQ:image") != std::string::npos &&
			 data.message.find("file") != std::string::npos &&
			 data.message.find("url=") != std::string::npos &&
			 data.message.find("file_size=") != std::string::npos &&
			 data.message.size() > 200) // 这个判断很蠢，我目前找不到更好的办法
	{
		this->provideImageRecognition(data.private_id, data.message, data.type);
	}
	else if (data.message.compare("#歌曲推荐") == 0)
	{
		data.type = "CQ";
		musicShareMessage(data.message, 1);
	}
	else if (data.message.compare("#帮助") == 0)
	{
		std::ifstream ifs(CManager.configVariable("HELP_PATH"));
		if (!ifs.is_open())
		{
			LOG_ERROR("帮助文件打开失败！请检查...");
			data.message = "正在编辑中 .>_<.";
		}
		else
		{
			data.message.assign((std::istreambuf_iterator<char>(ifs.rdbuf())), std::istreambuf_iterator<char>());
			ifs.close();
		}
	}
	else if (!strcmp(data.message.c_str(), "#人格帮助"))
	{
		std::ifstream ifs(CManager.configVariable("HELP_PERSONALITY_PATH"));
		if (!ifs.is_open())
		{
			LOG_ERROR("file open failed!");
			data.message = "正在编辑中 .>_<.";
		}
		else
		{
			data.message.assign((std::istreambuf_iterator<char>(ifs.rdbuf())), std::istreambuf_iterator<char>());
			ifs.close();
		}
	}
	else if (data.message.find("#设置人格:") != std::string::npos || data.message.find("#设置人格：") != std::string::npos)
	{
		this->setPersonality(data.message, data.private_id);
	}
	else if (data.message.find("#轻量型人格:") != std::string::npos || data.message.find("#轻量型人格：") != std::string::npos)
	{
		this->setPersonality(data.message, data.private_id, 1);
	}
	else if (data.message.find("#人格还原:") != std::string::npos || data.message.find("#人格还原：") != std::string::npos)
	{
		this->setPersonality(data.message, data.private_id);
	}
	else if (data.message.compare("#重置对话") == 0)
	{
		this->resetChat(data.message, data.private_id);
	}
	else if (data.message.find("#话题:") != std::string::npos || data.message.find("#话题：") != std::string::npos)
	{
		auto user = this->user_messages->find(data.private_id);
		if (user == this->user_messages->end())
		{
			this->addUsers(data.private_id);
		}
		user->second.user_chatHistory[0].first = (this->system_message_format + data.message + "\"},");
		user->second.user_chatHistory[0].second = time(nullptr);
		user->second.user_chatHistory[1].first = (this->system_message_format + "OK!I will use Chinses answer \"},");
		user->second.user_chatHistory[1].second = time(nullptr);

		data.message = "好的，接下来我会围绕此话题进行对话";
		std::lock_guard<std::mutex> lock(mutex_message);
		this->user_messages->find(data.private_id)->second = user->second;
	}
	else if (data.message.compare("#当前气温") == 0)
	{
	}
	else if (data.message.compare("#设置定时") == 0)
	{
		data.message = TTastClass.setFixedRemind(data.message, data.private_id);
	}
	else if (data.message.find("#切换模型") != std::string::npos || data.message.find("#模型切换") != std::string::npos)
	{
		this->switchModel(data.message, data.private_id);
	}
	else if (data.message.find("#查询当前模型") != std::string::npos)
	{
		data.message = "你当前的模型为：" + this->user_messages->find(data.private_id)->second.user_models.first;
	}
	else if (data.message.find("#开启语音") != std::string::npos)
	{
		if (!this->global_Voice)
		{
			data.message = "管理员临时关闭了该功能，可能是在维护...";
		}
		else
		{
			this->user_messages->find(data.private_id)->second.isOpenVoiceMode = true;
			data.message = "已开启！";
		}
	}
	else if (data.message.find("#关闭语音") != std::string::npos)
	{
		this->user_messages->find(data.private_id)->second.isOpenVoiceMode = false;
		data.message = "已关闭！";
	}
	else if (data.message.find("#生成图片：") != std::string::npos || data.message.find("#生成图片:") != std::string::npos)
	{
		data.type = "CQ";
		this->provideImageCreation(data.private_id, data.message);
	}
	else if (data.message.find("#删除上条对话") != std::string::npos)
	{
		data.message = this->removePreviousContext(data.private_id);
	}
	// else if (message.substr(1, 12).find(message.find("#SD绘图")) != message.npos)
	else if (data.message.find("#SD绘图") != std::string::npos)
	{
		data.type = "CQ";
		this->SDImageCreation(data.message);
	}
	else
	{
		characterMessage(data);
	}
	return data.message;
}

bool Message::messageFilter(std::string message_type, std::string message)
{
	// 过滤策略
	if (message_type.size() < 4)
		return false;
	if (!strcmp(message_type.c_str(), "group"))
	{
		if (CManager.configVariable("OPEN_GROUPCHAT_MESSAGE") == "false")
			return false; // 群消息是否开启

		if (message.find("CQ:at") == message.npos || message.find(CManager.configVariable("BOT_QQ")) == message.npos)
			return false; // 过滤非AT消息
	}

	// 检查CQ码，不对转发内容进行
	return true;
}

bool Message::removeGroupCQCode(std::string &message)
{
	// 为了防止CQ码和普通文本出现冲突，这里选择半硬编码查找，减少冲突，但不一定彻底解决
	std::string specificCQCode = "[CQ:at,qq=" + CManager.configVariable("BOT_QQ") + "]";

	int begin = message.find(specificCQCode);
	if (begin == message.npos)
	{
		LOG_WARNING("该消息CQ码格式异常或者不存在CQ码!\n源消息:" + message);
		return false;
	}
	int end = message.find("]");
	// 移除CQ码
	message.erase(begin, end - begin + 1);

	return true;
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

void Message::SpeechSound(std::string &message)
{
	message = "你好，我是克莱茵，请问有什么可以帮到你。";
}

void Message::characterMessage(JsonData &data)
{

	// 获取用户当前使用的模型
	auto models = this->user_messages->find(data.private_id)->second.user_models;

	// 是否开启上下文  	当上下文模式为开启状态 || 访问者是管理员时，启用上下文模式
	if (this->accessibility_chat || data.private_id == stoi(CManager.configVariable("MANAGER_QQ")))
	{
		int contextMax = 0;

		// 设置模型最大的上下文
		contextMax = stoi(CManager.configVariable("CONTEXT_MAX"));

		// 提取用户聊天记录
		std::vector<std::pair<std::string, time_t>> user_vector;
		{
			std::lock_guard<std::mutex> lock(mutex_message);
			user_vector = this->user_messages->find(data.private_id)->second.user_chatHistory;
		}

		// 判断消息存活时间
		if (user_vector.back().second + stoi(CManager.configVariable("MESSAGE_SURVIVAL_TIME")) < time(nullptr))
		{
			user_vector.erase(user_vector.begin() + 1, user_vector.end());
			LOG_WARNING("该用户的消息存活时间大于指定时间，已清空...");
		}
		else // 判断消息是否达到最大token限度
		{
			short c_size = 0; // 这里存储的是占用的字节数，而非token数
			// 这部分可以优化，现在为了进度先暂时搁置
			c_size = user_vector.begin()->first.size() + (user_vector.begin() + 1)->first.size(); // 提前统计首部长度
			for (auto it = user_vector.end() - 1; it > user_vector.begin() + 1; it--)
			{
				c_size += it->first.size();
				// 删除早期聊天记录
				if ((c_size / 3) >= contextMax - 512) //  预留512 token，保证判断正常
				{
					std::cout << "Chat message delete over!" << std::endl;
					user_vector.erase(user_vector.begin() + this->default_message_line, it + 2);
					std::lock_guard<std::mutex> lock(mutex_message);
					this->user_messages->find(data.private_id)->second.user_chatHistory = user_vector;
					break;
				}
			}
		}

		data.message = JParsingClass.toJson(data.message);
		std::string format = this->users_message_format + data.message + "\"}";
		user_vector.push_back(make_pair(format, time(nullptr)));

		data.message = "";
		for (std::vector<std::pair<std::string, time_t>>::const_iterator it = user_vector.begin(); it != user_vector.end(); it++)
		{
			data.message += "\t" + it->first + '\n';
		}
		data.message.insert(0, "[\n");
		data.message.insert(data.message.size(), "]");

		// 将内容发送至对接的大预言模型
		std::cout << "send to Model..." << std::endl;
		Dock::RequestGPT(data.message, models, &this->user_messages->find(data.private_id)->second);

		// 消息完整性验证
		std::string errorSubstr = data.message.substr(0, 10);
		// 判断此消息是否为错误消息
		if (errorSubstr.find("error") != errorSubstr.npos)
		{
			// 提取错误问题
			if (data.message.find("message"))
			{
				int begin_index = data.message.find("message") + 10;
				int end_index = data.message.find("type") - 3;
				data.message = data.message.substr(begin_index, end_index - begin_index);
				LOG_ERROR(data.message);
			}
			data.message.insert(0, "系统提示：未知错误,建议清除上下文后重新发送！\n错误消息：");
		}
		else if (data.message.size() > 100) // 当收到的消息大于100个字节则表示为合法消息
		{
			data.message = JParsingClass.getAttributeFromChoices(data.message, "content");

			user_vector.back().first.push_back(','); // 格式调整
			std::string JsonData = JParsingClass.toJson(data.message);

			// 保存回答信息
			format = this->bot_message_format + JsonData + "\"},"; // 数据格式化
			user_vector.push_back(make_pair(format, time(nullptr)));

			// 判断数据是否超出额定值
			short c_size = 0;

			// 这部分可以优化，现在为了进度先暂时搁置
			c_size = user_vector.begin()->first.size() + (user_vector.begin() + 1)->first.size(); // 提前统计首部长度
			for (auto it = user_vector.end() - 1; it > user_vector.begin() + 1; it--)
			{
				c_size += it->first.size();
				// 删除早期聊天记录
				if ((c_size / 3) >= contextMax - 512) //  预留512 token，保证判断正常
				{
					LOG_WARNING("Chat message delete over!");
					user_vector.erase(user_vector.begin() + this->default_message_line, it + 2);
					std::lock_guard<std::mutex> lock(mutex_message);
					this->user_messages->find(data.private_id)->second.user_chatHistory = user_vector;
					break;
				}
			}

			// 确认无误，聊天记录更新
			std::lock_guard<std::mutex> lock(mutex_message);
			this->user_messages->find(data.private_id)->second.user_chatHistory = user_vector;
		}
		else
		{
			LOG_ERROR("小于100字节的消息：" + data.message);
		}
	}
	else
	{
		// 不开启上下文
		LOG_INFO("当前聊天不支持上下文模式...");
		data.message = JParsingClass.toJson(data.message);
		std::string format = this->users_message_format + data.message + "\"}";
		data.message = format;
		data.message.insert(0, "[\n");
		data.message.insert(data.message.size(), "]");
		std::cout << "send to model..." << std::endl;
		Dock::RequestGPT(data.message,
						 this->user_messages->find(data.private_id)->second.user_models,
						 &this->user_messages->find(data.private_id)->second);
		if (data.message.size() > 100)
		{
			data.message = JParsingClass.getAttributeFromChoices(data.message, "content");
			// cout << "OpenAI response: " << message << endl;
		}
	}

	// 打印回复内容
	std::cout << "\033[32m"
			  << "OpenAI response: "
			  << "\033[0m" << data.message << std::endl;

	// 判断是否需要提供文本转语音
	if (this->global_Voice && this->user_messages->find(data.private_id)->second.isOpenVoiceMode)
	{
		// LOG_INFO(std::to_string(data.private_id) + "需要使用文本转语音...");
		data.type = "CQ";
		this->textToVoice(data.message, data.type);
	}
}

void Message::musicShareMessage(std::string &message, short platform)
{
	int num = 0;
	uint64_t songID = 0;
	switch (platform)
	{
	case 1:
	{
		num = rand() % Database::getInstance()->sID.getWyy_size();
		songID = Database::getInstance()->sID.getWyyID(num);
		message.insert(0, CQCode("music", "type", "163", "id", songID));
		break;
	}
	default:
		break;
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

/*
string Message::privateGOCQFormat(string message, uint64_t user_id)
{
	// 封装go-cq格式
	stringstream post_json;
	post_json << R"({"user_id":)" << user_id << R"(,"message":")" << message << R"("})";
	return post_json.str();
}

string Message::gourpGOCQFormat(string message, uint64_t group_id)
{
	stringstream json_data;
	json_data << R"({"group_id":)" << group_id << R"(,"message":")" << message << R"("})";
	return json_data.str();
}
*/

void Message::atAllMessage(std::string &message)
{
	message = CQCode("at", "all");
}

void Message::setPersonality(std::string &roleName, uint64_t user_id)
{
	// 判断用户是否存在
	if (this->user_messages->find(user_id) == this->user_messages->begin())
	{
		this->addUsers(user_id);
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
	roleName = roleName.substr(result);

	if (roleName.find("人格还原") != roleName.npos)
	{
		this->mutex_message.lock();
		auto user = this->user_messages->find(user_id);
		this->mutex_message.unlock();

		user->second.user_chatHistory[0].first = this->system_message_format + this->default_personality;
		user->second.temperature = CManager.configVariable("temperature");
		user->second.top_p = CManager.configVariable("top_p");
		user->second.frequency_penalty = CManager.configVariable("frequency_penalty");
		user->second.presence_penalty = CManager.configVariable("presence_penalty");

		// 同步
		std::lock_guard<std::mutex> lock(this->mutex_message);
		this->user_messages->find(user_id) = user;

		roleName = "人格已还原";
	}

	// 读取人格文件
	std::string path = CManager.configVariable("PERSONALITY_PATH") + roleName + ".txt";
	std::ifstream ifs(path);
	if (!ifs.is_open())
	{
		LOG_WARNING("文件打开失败，文件路径:" + path);
		roleName = "系统提示：“" + roleName + "”人格不存在！";
		return;
	}
	std::stringstream buffer;
	buffer << ifs.rdbuf();

	// 提取数据
	std::string originData = buffer.str();
	std::string personality;
	std::string temperature;
	std::string top_p;
	std::string frequency_penalty;
	std::string presence_penalty;
	size_t begin = 0;
	size_t range = 0;

	begin = originData.find("Personlity") + 12;
	range = originData.find("}") - begin;
	if (begin == std::string::npos || range < 1)
	{
		LOG_ERROR("配置文件有误！");
		roleName = "系统提示：“" + roleName + "”人格未找到！";
		return;
	}
	personality = originData.substr(begin, range);
	personality = JParsingClass.toJson(personality);
	originData.erase(0, originData.find("}") + 1);

	begin = originData.find("Temperature") + 13;
	range = originData.find("}") - begin;
	if (begin == std::string::npos || range < 1)
	{
		LOG_ERROR("配置文件有误！");
		roleName = "系统提示：“" + roleName + "”人格未找到！";
		return;
	}
	temperature = originData.substr(begin, range);
	originData.erase(0, originData.find("}") + 1);

	begin = originData.find("Top_p") + 7;
	range = originData.find("}") - begin;
	if (begin == std::string::npos || range < 1)
	{
		LOG_ERROR("配置文件有误！");
		roleName = "系统提示：“" + roleName + "”人格未找到！";
		return;
	}
	top_p = originData.substr(begin, range);
	originData.erase(0, originData.find("}") + 1);

	begin = originData.find("Frequency_penalty") + 19;
	range = originData.find("}") - begin;
	if (begin == std::string::npos || range < 1)
	{
		LOG_ERROR("配置文件有误！");
		roleName = "系统提示：“" + roleName + "”人格未找到！";
		return;
	}
	frequency_penalty = originData.substr(begin, range);
	originData.erase(0, originData.find("}") + 1);

	begin = originData.find("Presence_penalty") + 18;
	range = originData.find("}") - begin;
	if (begin == std::string::npos || range < 1)
	{
		LOG_ERROR("配置文件有误！");
		roleName = "系统提示：“" + roleName + "”人格未找到！";
		return;
	}
	presence_penalty = originData.substr(begin, range);
	originData.erase(0, originData.find("}") + 1);

	// 参数载入
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

	roleName = "启用“" + roleName + "”人格。";
}

void Message::setPersonality(std::string &roleName, uint64_t user_id, int tag)
{
	// 判断用户是否存在
	if (this->user_messages->find(user_id) == this->user_messages->begin())
	{
		this->addUsers(user_id);
	}

	if (this->LightweightPersonalityList.size() < 1)
	{
		LOG_ERROR("轻量型人格数据库为空！");
		roleName = "设置失败！";
	}

	bool flog = true;
	for (auto it = this->LightweightPersonalityList.begin(); it != LightweightPersonalityList.end(); it++)
	{
		if (roleName.find(it->first) != roleName.npos)
		{
			roleName = it->second;
			flog = false;
			break;
		}
	}

	if (flog)
	{
		LOG_WARNING("人格参数未空或不为内置参数");
		roleName = "系统提示：未找到此人格！";
		return;
	}

	std::lock_guard<std::mutex> lock(mutex_message);
	this->user_messages->find(user_id)->second.user_chatHistory[0].first = system_message_format + roleName;
	roleName = "设置成功";
}

void Message::resetChat(std::string &roleName, uint64_t user_id)
{
	auto user = this->user_messages->find(user_id);
	if (user == this->user_messages->end())
	{
		this->addUsers(user_id);
	}
	else
	{
		// 重置对话会删除之前的所有信息，包括人格信息
		user->second.user_chatHistory.clear();
		user->second.user_chatHistory.push_back(make_pair(this->system_message_format + this->default_personality, time(nullptr)));
		user->second.user_chatHistory.push_back(
			make_pair(this->bot_message_format + "Ok, I will strictly abide by the above regulations and answer questions in Chinese.\"},", time(nullptr)));
		std::lock_guard<std::mutex> lock(mutex_message);
		this->user_messages->find(user_id)->second = user->second;
	}
	roleName = "会话重置完成！";
}

bool Message::adminTerminal(std::string &message, uint64_t user_id)
{
	std::string str = message;
	/*if (message.find("#添加图片") != message.npos)
	{
		if (Database::getInstance()->AP.savePictrueURL(message))
		{
			message = "添加成功";
		}
		else
			message = "添加失败";
	}

	else if (message.find("CQ:image") != message.npos)
	{
		database::getInstance()->imgURL.saveFaceURL(message);
		facePackageMessage(message);
	}
	*/
	if (message.find("#开启无障碍聊天") != message.npos)
	{
		this->accessibility_chat = true;
		message = "无障碍聊天已开启！";
	}
	else if (message.find("#关闭无障碍聊天") != message.npos)
	{
		this->accessibility_chat = false;
		message = "无障碍聊天已关闭！";
	}
	else if (message.find("#刷新配置文件") != message.npos)
	{
		CManager.refreshConfiguation("config.json");
		message = "配置文件已刷新";
	}
	else if (message.find("#激活语音") != message.npos)
	{
		this->global_Voice = true;
		message = "已激活！";
	}
	else if (message.find("#冻结语音") != message.npos)
	{
		this->global_Voice = false;
		message = "已冻结！";
	}
	else if (message.find("#获取服务器inet4") != message.npos)
	{
		message = this->PCStatus->getInet4();
	}
	else if (message.find("#获取服务器inet6") != message.npos)
	{
		message = this->PCStatus->getInet6();
	}
	else if (message.find("#获取服务器公网IP") != message.npos)
	{
		message = this->PCStatus->getPublicIP4();
	}
	// 若message被修改，判断为走内置消息
	return str != message ? true : false;
}

void Message::switchModel(std::string &message, uint64_t user_id)
{
	if (message.size() < 14)
	{
		LOG_WARNING("未指定模型...");
		message = "系统提示：请选择要切换的模型！";
		return;
	}

	// 模型名称提取
	if (message.find(":") != std::string::npos)
	{
		message = message.substr(message.find(":") + 1);
	}
	else
	{
		message = message.substr(message.find("：") + 3);
	}
	std::string modelName = message;

	// 消除后缀空格
	while (modelName.back() == ' ')
	{
		modelName.erase(modelName.size() - 1, 1);
	}

	// 寻找相同的模型名称
	for (auto GPTModel = this->chatModels.begin(); GPTModel != this->chatModels.end(); GPTModel++)
	{
		if (GPTModel->first == modelName)
		{
			this->user_messages->find(user_id)->second.user_models = *GPTModel;
			message = "设置成功，当前模型为:" + this->user_messages->find(user_id)->second.user_models.first;
			return;
		}
	}
	message = "系统提示：不存在的模型!";
}

bool Message::permissionVerification(uint64_t user_id)
{
	return user_id == stoi(CManager.configVariable("MANAGER_QQ")) ? true : false;
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
	int res = rlg->fixImageSizeTo4K(message);

	if (res == -1)
	{
		LOG_ERROR("返回内容少于20字节");
		return;
	}
	if (res == 1)
	{
		// 路径传输
		message.insert(0, "file://");
		message = CQCode("image", "file", message);
	}
	else if (res == 2)
	{
		// base64传输
		message = CQCode("image", "file=base64://" + message);
	}

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

bool Message::provideImageRecognition(const uint64_t user_id, std::string &message, std::string &type)
{
	std::string conversation = message.substr(0, message.find("[CQ:image"));									// 提取对话，如果有的话
	std::string url = message.substr(message.find("url=") + 4, message.find("]") - (message.find("url=") + 4)); // 提取URL

	// URL纠正
	size_t index = 0;
	do
	{
		std::string findWord = "&amp;";
		index = url.find(findWord);
		if (index != std::string::npos)
		{
			url.erase(index, findWord.size());
			url.insert(index, "&");
		}
	} while (index != std::string::npos);

	// 若不存在prompt，则设置默认prompt
	if (conversation.size() < 6)
	{
		conversation = "Please analyze this picture in all aspects and answer it in Chinese";
	}

	// 初始化
	CURL *curl_handle = curl_easy_init();
	if (!curl_handle)
	{
		LOG_ERROR("Failed to initialize curl handle.");
		return false;
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
		curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &input);

		res = curl_easy_perform(curl_handle);

		if (res != CURLE_OK)
		{
			LOG_ERROR("Failed to download image: " + std::string(curl_easy_strerror(res)));
			curl_easy_cleanup(curl_handle);
			return false;
		}
	}
	else
	{
		LOG_ERROR("Failed to initialize curl handle.");
		return false;
	}
	curl_easy_cleanup(curl_handle);

	LOG_INFO("图片下载完成");

	// 下载完成，将数据转为base64编码
	std::string base64 = this->dataToBase64(input);

	// 封装消息，向OpenAI发送  这里可以检查收到的信息是否合法
	std::cout << "send to vision model..." << std::endl;
	OpenAIStandard::send_to_vision(conversation, base64,
								   CManager.configVariable("VISION_MODEL"),
								   CManager.configVariable("VISION_MODEL_ENDPOINT"),
								   CManager.configVariable("VISION_MODEL_API_KEY"));

	// 获取结果
	message = conversation;
	if (message.size() > 100)
	{
		message = JParsingClass.getAttributeFromChoices(message, "content");
		if (message.size() < 10)
		{
			LOG_ERROR(conversation);
		}
		else
		{
			std::cout << "OpenAI response: " << message << std::endl;
			// 判断是否需要转语音
			if (this->user_messages->find(user_id)->second.isOpenVoiceMode)
			{
				this->textToVoice(message, type);
			}
		}
	}
	else
	{
		message = "系统提示：分析超时，请重试。";
		return false;
	}
	return true;
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
int Message::textToVoice(std::string &text, std::string &type)
{
	type = "CQ";
	text = JParsingClass.toJson(text);
	// LOG_DEBUG("转语音的内容：" + text);
	int res = this->voice->toAudio(text);
	if (res = 1)
	{
		// 使用路径传输
		std::string URL = "file://" + text;
		text = CQCode("record", "file", URL);
		return 0;
	}
	else if (res = 2)
	{
		// 使用base64编码传输
		// 很遗憾的是，record不支持base64编码
		LOG_WARNING("开发出现错误，你使用了base64编码的方式传输，但是CQ码并不支持这种方式...");
		return -1;
	}
	else
	{
		// 错误返回内容
		return -1;
	}
}

bool Message::provideImageCreation(const uint64_t user_id, std::string &text)
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
		text = "系统提示：描述过短！至少存在1个汉字或者3个字符...";
		return false;
	}

	// 开始请求OpenAI
	std::cout << "send to Model..." << std::endl;
	OpenAIStandard::text_translate(prompt,
								   CManager.configVariable("TEXTTRANSLATE_MODEL"),
								   "EN", CManager.configVariable("TEXTTRANSLATE_MODEL_ENDPOINT"),
								   CManager.configVariable("TEXTTRANSLATE_MODEL_API_KEY"));
	LOG_INFO("文本翻译完成。");
	prompt = JParsingClass.toJson(prompt);
	std::pair<std::string, std::string> p;
	p.first = CManager.configVariable("DRAW_MODEL");
	p.second = CManager.configVariable("DRAW_MODEL_APISTANDARD");
	Dock::RequestGPT(prompt, p, &this->user_messages->find(user_id)->second);

	if (prompt.size() < 100)
	{
		prompt = "网络异常...";
		return false;
	}

	std::string url;
	if (prompt.rfind("invalid_request_error") != prompt.npos)
	{
		LOG_ERROR("触发官方保护!");
		prompt = prompt.substr(prompt.find("message") + 10); // 前缀分割
		text = prompt.substr(0, prompt.find("request id") - 10);
		url = "";
		// LOG_DEBUG(text);
		return false;
	}
	else
	{
		// Json解析
		bool result = JParsingClass.findKeyAndValue(prompt, "url", url);
		if (!result)
		{
			text = "Json数据中未找到URL";
			LOG_ERROR("Json数据中未找到URL，源数据为：" + prompt);
			return false;
		}

		std::string outputPath;

		// 封装格式(采用RUL)
		outputPath = url;
		// 格式封装
		// outputPath.insert(0, "file://"); // 采用绝对路径
		prompt = CQCode("image", "file", outputPath); // 采用网路路径
		text = prompt;
		return true;
	}
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

void Message::SDImageCreation(std::string &message)
{
	// 提取出prompt
	if (message.size() < 14)
	{
		LOG_WARNING("用户未描述图像");
		message = "系统提示：请描述图像...";
		return;
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
	OpenAIStandard::text_translate(prompt,
								   CManager.configVariable("TEXTTRANSLATE_MODEL"),
								   "EN", CManager.configVariable("TEXTTRANSLATE_MODEL_ENDPOINT"),
								   CManager.configVariable("TEXTTRANSLATE_MODEL_API_KEY"));

	// 调用StableDiffusion
	std::string base64_code;
	base64_code = StableDiffusion::connectStableDiffusion(prompt);

	if (base64_code.size() < 128)
	{
		message = "系统提示：数据返回有误！";
		return;
	}
	message = "[CQ:image,file=base64://";
	message.append(base64_code);
	message.append("]");
}

Message::~Message()
{
	if (this->user_messages != nullptr)
	{
		delete this->user_messages;
		this->user_messages = nullptr;
	}

	if (this->PCStatus != nullptr)
	{
		delete this->PCStatus;
		this->PCStatus = nullptr;
	}

	if (this->voice != nullptr)
	{
		delete this->voice;
		this->voice = nullptr;
	}
}