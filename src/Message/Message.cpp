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

	// 内置成员属性初始化
	this->user_messages = new map<UINT64, Person>;
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
			LOG_DEBUG("JSON数据解析失败！请检查Json文件的合法性。");
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
						(model.HasMember("service") && model["service"].IsString()))
					{
						pair<string, string> p;
						p.first = model["name"].GetString();
						p.second = model["service"].GetString();
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
	string path = CManager.configVariable("PERSONALITY_PATH") + "personality.txt";
	ifstream ifs(path);
	if (!ifs.is_open())
	{
		perror("轻量型人格初始化失败");
	}
	else
	{
		string line;
		while (!ifs.eof())
		{
			std::getline(ifs, line);
			size_t pos = line.find("|");
			if (pos != line.npos)
			{
				string key = line.substr(0, pos);
				string value = line.substr(pos + 1);
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

bool Message::addUsers(UINT64 user_id)
{
	if (user_messages->find(user_id) == user_messages->end())
	{
		// 添加默认数据
		vector<pair<string, time_t>> userDefault;
		pair<string, time_t> p;
		p.first = this->system_message_format + this->default_personality; // 设置system信息
		p.second = time(nullptr);										   // 获取当前时间
		userDefault.push_back(p);

		p.first = this->bot_message_format + "OK!I will use Chinses answer\"},"; // 设置assistant信息
		userDefault.push_back(p);

		// 创建用户
		Person person;
		person.user_chatHistory = userDefault;

		pair<string, string> models;
		models.first = CManager.configVariable("DEFAULT_MODEL"); // 默认模型
		models.second = CManager.configVariable("DEFAULT_MODEL_SERVICE");
		person.user_models = models;

		person.isOpenVoiceMode = false;
		person.temperature = CManager.configVariable("temperature");
		person.top_p = CManager.configVariable("top_p");
		person.frequency_penalty = CManager.configVariable("frequency_penalty");
		person.presence_penalty = CManager.configVariable("presence_penalty");

		std::lock_guard<std::mutex> lock(mutex_message);
		this->user_messages->insert(make_pair(user_id, person));
		return true;
	}
	return false;
}

string Message::handleMessage(UINT64 user_id, string message, string message_type)
{
	// 新建用户
	if (this->user_messages->find(user_id) == this->user_messages->end())
	{
		this->addUsers(user_id);
	}

	// 判断是否是群消息
	if (message_type == "group")
	{
		// 移除CQ码
		this->removeGroupCQCode(message);
	}

	// 显示用户输入的问题
	cout << "[" << message_type << "]" << user_id << ":" << message << endl;

	// 管理员权限判断
	if (this->permissionVerification(user_id))
	{
		// 管理员可操作的命令
		bool result = this->adminTerminal(message, user_id);
		if (result)
		{
			return message;
		}
	}

	// 内置回复判断
	/*
	if (message.find("#修复图片") != message.npos)
	{
		// 该功能于v2.2.0版本将被遗弃
		// this->fixImageSizeTo4K(message);
		// message = "系统提示：该功能正在升级...";
	}
	else if (message.find("#精美图片") != message.npos)
	{
		questPictureID(message);
		LOG_DEBUG(message);
	}
	*/
	if (message == CManager.configVariable("QBOT_NAME"))
	{
		SpeechSound(message);
	}
	else if (message.find("CQ:image") != message.npos)
	{
		/*该功能在早期版本是斗图功能，在v2.2.0版本被升级为GPT识图
		Database::getInstance()->imgURL.saveFaceURL(message);
		*/
		// facePackageMessage(message);

		this->provideImageRecognition(user_id, message);
	}
	else if (!strcmp(message.c_str(), "#歌曲推荐"))
	{
		musicShareMessage(message, 1);
	}
	else if (!strcmp(message.c_str(), "#帮助"))
	{
		ifstream ifs(CManager.configVariable("HELP_PATH"));
		if (!ifs.is_open())
		{
			LOG_ERROR("帮助文件打开失败！请检查...");
			message = "正在编辑中 .>_<.";
		}
		else
		{
			message.assign((istreambuf_iterator<char>(ifs.rdbuf())), istreambuf_iterator<char>());
			ifs.close();
		}
	}
	else if (!strcmp(message.c_str(), "#人格帮助"))
	{
		ifstream ifs(CManager.configVariable("HELP_PERSONALITY_PATH"));
		if (!ifs.is_open())
		{
			LOG_ERROR("file open failed!");
			message = "正在编辑中 .>_<.";
		}
		else
		{
			message.assign((istreambuf_iterator<char>(ifs.rdbuf())), istreambuf_iterator<char>());
			ifs.close();
		}
	}
	else if (message.find("#设置人格：") != message.npos || message.find("#设置人格:") != message.npos)
	{
		this->setPersonality(message, user_id);
	}
	else if (message.find("#轻量型人格：") != message.npos || message.find("#轻量型人格:") != message.npos)
	{
		this->setPersonality(message, user_id, 1);
	}
	else if (message.find("#人格还原：") != message.npos || message.find("#人格还原:") != message.npos)
	{
		this->setPersonality(message, user_id);
	}
	else if (message.find("#重置对话") != message.npos)
	{
		this->resetChat(message, user_id);
	}
	else if (message.find("#话题：") != message.npos || message.find("#话题:") != message.npos)
	{
		auto user = this->user_messages->find(user_id);
		if (user == this->user_messages->end())
		{
			this->addUsers(user_id);
		}
		user->second.user_chatHistory[0].first = (this->system_message_format + message + "\"},");
		user->second.user_chatHistory[0].second = time(nullptr);
		user->second.user_chatHistory[1].first = (this->system_message_format + "OK!I will use Chinses answer \"},");
		user->second.user_chatHistory[1].second = time(nullptr);

		message = "好的，接下来我会围绕此话题进行对话";
		std::lock_guard<std::mutex> lock(mutex_message);
		this->user_messages->find(user_id)->second = user->second;
	}
	else if (message.find("#当前气温") != message.npos)
	{
	}
	else if (message.find("#设置定时") != message.npos)
	{
		message = TTastClass.setFixedRemind(message, user_id);
	}
	else if (message.find("#切换模型") != message.npos || message.find("#模型切换") != message.npos)
	{
		this->switchModel(message, user_id);
	}
	else if (message.find("#查询当前模型") != message.npos)
	{
		message = "你当前的模型为：" + this->user_messages->find(user_id)->second.user_models.first;
	}
	else if (message.find("#开启语音") != message.npos)
	{
		if (!this->global_Voice)
		{
			message = "管理员临时关闭了该功能，可能是在维护...";
		}
		else
		{
			this->user_messages->find(user_id)->second.isOpenVoiceMode = true;
			message = "已开启！";
		}
	}
	else if (message.find("#关闭语音") != message.npos)
	{
		this->user_messages->find(user_id)->second.isOpenVoiceMode = false;
		message = "已关闭！";
	}
	else if (message.find("#生成图片：") != message.npos || message.find("#生成图片:") != message.npos)
	{
		this->provideImageCreation(user_id, message);
	}
	else if (message.find("#删除上条对话") != message.npos)
	{
		message = this->removePreviousContext(user_id);
	}
	// else if (message.substr(1, 12).find(message.find("#SD绘图")) != message.npos)
	else if (message.find("#SD绘图") != message.npos)
	{
		this->SDImageCreation(message);
	}
	else
	{
		characterMessage(user_id, message);
	}
	return message;
}

bool Message::messageFilter(string message_type, string message)
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
	return true;
}

bool Message::removeGroupCQCode(string &message)
{
	// 为了防止CQ码和普通文本出现冲突，这里选择半硬编码查找，减少冲突，但不一定彻底解决
	string specificCQCode = "[CQ:at,qq=" + CManager.configVariable("BOT_QQ") + "]";

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
void recursion(stringstream &cq, int &count, T arg)
{
	cq << arg << "]";
}

template <typename First, typename... Args>
void recursion(stringstream &cq, int &count, First first_arg, Args... args)
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
string CQCode(First first_args, Args... args)
{
	// 获取传入的参数个数
	int count = 0;
	stringstream cq;
	cq << "[CQ:" << first_args << ",";
	recursion(cq, count, args...);

	return cq.str();
}

void Message::questPictureID(string &message)
{
	std::uniform_int_distribution<int> dist(0, Database::getInstance()->CIU.getSize() - 1);
	message = CQCode("image", "file", Database::getInstance()->CIU.getCURL(dist(mt_rand)));
}

void Message::SpeechSound(string &message)
{
	message = "你好，我是克莱茵，请问有什么可以帮到你。";
	// this->textToVoice(message, user_id);
}

void Message::characterMessage(UINT64 &user_id, string &message)
{

	// 获取用户当前使用的模型
	auto models = this->user_messages->find(user_id)->second.user_models;

	// 是否开启上下文  	当上下文模式为开启状态 || 访问者是管理员时，启用上下文模式
	if (this->accessibility_chat || user_id == stoi(CManager.configVariable("MANAGER_QQ")))
	{
		int contextMax = 0;

		// 设置模型最大的上下文
		contextMax = stoi(CManager.configVariable("CONTEXT_MAX"));

		// 提取用户聊天记录
		vector<pair<string, time_t>> user_vector;
		{
			std::lock_guard<std::mutex> lock(mutex_message);
			user_vector = this->user_messages->find(user_id)->second.user_chatHistory;
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
					cout << "Chat message delete over!" << endl;
					user_vector.erase(user_vector.begin() + this->default_message_line, it + 2);
					std::lock_guard<std::mutex> lock(mutex_message);
					this->user_messages->find(user_id)->second.user_chatHistory = user_vector;
					break;
				}
			}
		}

		message = JParsingClass.toJson(message);
		string format = this->users_message_format + message + "\"}";
		user_vector.push_back(make_pair(format, time(nullptr)));

		message = "";
		for (vector<pair<string, time_t>>::const_iterator it = user_vector.begin(); it != user_vector.end(); it++)
		{
			message += "\t" + it->first + '\n';
		}
		message.insert(0, "[\n");
		message.insert(message.size(), "]");

		// 将内容发送至对接的大预言模型
		cout << "send to Model..." << endl;
		Dock::RequestGPT(message, models, &this->user_messages->find(user_id)->second);

		// 消息完整性验证
		string errorSubstr = message.substr(0, 10);
		// 判断此消息是否为错误消息
		if (errorSubstr.find("error") != errorSubstr.npos)
		{
			// 提取错误问题
			if (message.find("message"))
			{
				int begin_index = message.find("message") + 10;
				int end_index = message.find("type") - 3;
				message = message.substr(begin_index, end_index - begin_index);
				LOG_ERROR(message);
			}
			message.insert(0, "系统提示：未知错误,建议清除上下文后重新发送！\n错误消息：");
		}
		else if (message.size() > 100) // 当收到的消息大于100个字节则表示为合法消息
		{
			message = JParsingClass.getAttributeFromChoices(message, "content");

			user_vector.back().first.push_back(','); // 格式调整
			string JsonData = JParsingClass.toJson(message);

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
					this->user_messages->find(user_id)->second.user_chatHistory = user_vector;
					break;
				}
			}

			// 确认无误，聊天记录更新
			std::lock_guard<std::mutex> lock(mutex_message);
			this->user_messages->find(user_id)->second.user_chatHistory = user_vector;
		}
		else
		{
			LOG_ERROR("小于100字节的消息：" + message);
		}
	}
	else
	{
		// 不开启上下文
		LOG_INFO("当前聊天不支持上下文模式...");
		message = JParsingClass.toJson(message);
		string format = this->users_message_format + message + "\"}";
		message = format;
		message.insert(0, "[\n");
		message.insert(message.size(), "]");
		cout << "send to Model..." << endl;
		Dock::RequestGPT(message,
						 this->user_messages->find(user_id)->second.user_models,
						 &this->user_messages->find(user_id)->second);
		if (message.size() > 100)
		{
			message = JParsingClass.getAttributeFromChoices(message, "content");
			// cout << "OpenAI response: " << message << endl;
		}
	}

	// 打印回复内容
	std::cout << "\033[32m"
			  << "OpenAI response: "
			  << "\033[0m" << message << std::endl;

	// 判断是否需要提供文本转语音
	if (this->global_Voice && this->user_messages->find(user_id)->second.isOpenVoiceMode)
	{
		LOG_INFO(std::to_string(user_id) + "需要使用文本转语音...");
		this->textToVoice(message, user_id);
	}
}

void Message::musicShareMessage(string &message, short platform)
{
	int num = 0;
	UINT64 songID = 0;
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

void Message::facePackageMessage(string &message)
{
	message.clear();
	int num = rand() % Database::getInstance()->imgURL.getIMGURL_size() + 1; // 表情包ID
	message.insert(0, CQCode("image", "file", Database::getInstance()->imgURL.getIMG_URL(num)));
}

string Message::atUserMassage(string message, UINT64 user_id)
{
	message.insert(0, CQCode("at", "qq", user_id));
	return message;
}

/*
string Message::privateGOCQFormat(string message, UINT64 user_id)
{
	// 封装go-cq格式
	stringstream post_json;
	post_json << R"({"user_id":)" << user_id << R"(,"message":")" << message << R"("})";
	return post_json.str();
}

string Message::gourpGOCQFormat(string message, UINT64 group_id)
{
	stringstream json_data;
	json_data << R"({"group_id":)" << group_id << R"(,"message":")" << message << R"("})";
	return json_data.str();
}
*/

void Message::atAllMessage(string &message)
{
	message = CQCode("at", "all");
}

void Message::setPersonality(string &roleName, UINT64 user_id)
{
	// 判断用户是否存在
	if (this->user_messages->find(user_id) == this->user_messages->begin())
	{
		this->addUsers(user_id);
	}

	// 提取人格名称
	auto result = roleName.find(":");
	if (result != string::npos)
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
		std::lock_guard<std::mutex> lock(mutex_message);
		this->user_messages->find(user_id) = user;

		roleName = "人格已还原";
	}

	// 读取人格文件
	string path = CManager.configVariable("PERSONALITY_PATH") + roleName + ".txt";
	ifstream ifs(path);
	if (!ifs.is_open())
	{
		LOG_WARNING("文件打开失败，文件路径:" + path);
		roleName = "系统提示：“" + roleName + "”人格不存在！";
		return;
	}
	stringstream buffer;
	buffer << ifs.rdbuf();

	// 提取数据
	string originData = buffer.str();
	string personality;
	string temperature;
	string top_p;
	string frequency_penalty;
	string presence_penalty;
	size_t begin = 0;
	size_t range = 0;

	begin = originData.find("Personlity") + 12;
	range = originData.find("}") - begin;
	if (begin == string::npos || range < 1)
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
	if (begin == string::npos || range < 1)
	{
		LOG_ERROR("配置文件有误！");
		roleName = "系统提示：“" + roleName + "”人格未找到！";
		return;
	}
	temperature = originData.substr(begin, range);
	originData.erase(0, originData.find("}") + 1);

	begin = originData.find("Top_p") + 7;
	range = originData.find("}") - begin;
	if (begin == string::npos || range < 1)
	{
		LOG_ERROR("配置文件有误！");
		roleName = "系统提示：“" + roleName + "”人格未找到！";
		return;
	}
	top_p = originData.substr(begin, range);
	originData.erase(0, originData.find("}") + 1);

	begin = originData.find("Frequency_penalty") + 19;
	range = originData.find("}") - begin;
	if (begin == string::npos || range < 1)
	{
		LOG_ERROR("配置文件有误！");
		roleName = "系统提示：“" + roleName + "”人格未找到！";
		return;
	}
	frequency_penalty = originData.substr(begin, range);
	originData.erase(0, originData.find("}") + 1);

	begin = originData.find("Presence_penalty") + 18;
	range = originData.find("}") - begin;
	if (begin == string::npos || range < 1)
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

void Message::setPersonality(string &roleName, UINT64 user_id, int tag)
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

void Message::resetChat(string &roleName, UINT64 user_id)
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

bool Message::adminTerminal(string &message, UINT64 user_id)
{
	string str = message;
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

void Message::switchModel(string &message, UINT64 user_id)
{
	if (message.size() < 14)
	{
		LOG_WARNING("未指定模型...");
		message = "系统提示：请选择要切换的模型！";
		return;
	}

	// 模型名称提取
	if (message.find(":") != string::npos)
	{
		message = message.substr(message.find(":") + 1);
	}
	else
	{
		message = message.substr(message.find("：") + 3);
	}
	string modelName = message;

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

bool Message::permissionVerification(UINT64 user_id)
{
	return user_id == stoi(CManager.configVariable("MANAGER_QQ")) ? true : false;
}

// 回调函数用于写入数据到文件
size_t write_data(void *ptr, size_t size, size_t nmemb, std::string *data)
{
	data->append(reinterpret_cast<const char *>(ptr), size * nmemb);
	return size * nmemb;
}
bool Message::fixImageSizeTo4K(string &message)
{
	std::string url = message.substr(message.find("url=") + 4, message.find(";") - (message.find("url=") + 4)); // 提取URL
	if (url.size() < 10)
	{
		message = "似乎没有图片...";
		return false;
	}

	// URL修正
	size_t index = 0;
	do
	{
		string findWord = "&amp;";
		index = url.find(findWord);
		if (index != string::npos)
		{
			url.erase(index, findWord.size());
			url.insert(index, "&");
		}
	} while (index != string::npos);

	string filename;
	// 随机名称
	for (int i = 0; i < 10; i++)
	{
		unsigned short number = rand() % 26 + 65;
		filename += (char)number;
	}

	filename += ".jpg";
	string inputPath = CManager.configVariable("IMAGE_DOWNLOAD_PATH") + filename;
	string outputPath = inputPath;
	outputPath.insert(outputPath.rfind("."), "_4K");

	// 下载图片
	{
		// 以二进制写入方式打开文件
		std::ofstream outfile(inputPath, std::ios::binary);
		if (!outfile.is_open())
		{
			std::cerr << "Failed to open file for writing" << std::endl;
			return false;
		}

		// 初始化 curl
		CURL *curl = curl_easy_init();
		if (!curl)
		{
			std::cerr << "Failed to initialize curl" << std::endl;
			outfile.close();
			return false;
		}

		// 创建 curl 实例
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outfile);

		// 执行下载操作
		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK)
		{
			std::cerr << "Failed to download image: " << curl_easy_strerror(res) << std::endl;
			outfile.close();
			curl_easy_cleanup(curl);
			return false;
		}

		// 关闭文件
		outfile.close();

		// 清理 curl 实例
		curl_easy_cleanup(curl);
	}

	// 对图片进行4K修复
	{
		string command = "" + CManager.configVariable("REALESGAN_PATH") + "realesrgan-ncnn-vulkan -i ";
		command += inputPath + " -o ";
		command += outputPath + " -n " + CManager.configVariable("REALESGAN_MODEL");
		// command += " > log.txt";

		LOG_INFO("正在进行修复...");
		int returnCode = system(command.c_str());
		if (returnCode == 0)
		{
			LOG_INFO("修复完成！");
		}
		else
		{
			LOG_ERROR("修复失败！");
			message = "系统提示：修复失败!";
			return false;
		}
	}

	// 发送已经修复的图片
	outputPath.insert(0, "file://");
	message = CQCode("image", "file", outputPath);
	return true;
}

// 数据流转为base64编码
string Message::dataToBase64(const string &input)
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

string Message::encodeToURL(const string &input)
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

bool Message::provideImageRecognition(const UINT64 user_id, string &message)
{
	std::string conversation = message.substr(0, message.find("[CQ:image"));									// 提取对话，如果有的话
	std::string url = message.substr(message.find("url=") + 4, message.find("]") - (message.find("url=") + 4)); // 提取URL

	// URL纠正
	size_t index = 0;
	do
	{
		string findWord = "&amp;";
		index = url.find(findWord);
		if (index != string::npos)
		{
			url.erase(index, findWord.size());
			url.insert(index, "&");
		}
	} while (index != string::npos);

	// 若不存在prompt，则设置默认prompt
	if (conversation.size() < 6)
	{
		conversation = "Please analyze this picture in all aspects and answer it in Chinese";
	}

	// 初始化
	// this->mutex_message.lock();
	CURL *curl_handle = curl_easy_init();
	// this->mutex_message.unlock();
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
		curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &input);

		res = curl_easy_perform(curl_handle);

		if (res != CURLE_OK)
		{
			LOG_ERROR("Failed to download image: ");
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
	string base64 = this->dataToBase64(input);

	// 封装消息，向OpenAI发送  这里可以检查收到的信息是否合法
	cout << "send to OpenAI..." << endl;
	OpenAIStandard::send_to_vision(conversation, base64,
								   CManager.configVariable("VISION_DEFAULT_MODEL"),
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
			cout << "OpenAI response: " << message << endl;
			// 判断是否需要转语音
			if (this->user_messages->find(user_id)->second.isOpenVoiceMode)
			{
				this->textToVoice(message, user_id);
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
void Message::textToVoice(string &text, const UINT64 user_id)
{
	LOG_INFO("使用了文本转语音");
	/*
	 * 目前文本转语音仅支持中文
	 */

	// 检查文本是否合格
	if (text.size() < 4)
	{
		text = "系统提示：未检查到文本...";
		return;
	}

	// 创建语音文件
	string filename;
	for (int i = 0; i < 10; i++)
	{
		unsigned short number = rand() % 26 + 65;
		filename += (char)number;
	}
	filename += ".wav";

	string filePath = CManager.configVariable("VIST_FILE_SAVE_PATH") + filename;
	std::ofstream tts_file(filePath, std::ios::binary);
	if (!tts_file.is_open())
	{
		LOG_WARNING(filePath);
		LOG_ERROR("Unable to open output file.");
		return;
	}

	// 判断参考文件是否存在
	if (!ifstream(CManager.configVariable("VIST_REFERVOICE_PATH")).is_open())
	{
		LOG_ERROR("参考音频不存在，无法推理！请检查路径：" + CManager.configVariable("VIST_REFERVOICE_PATH"));
		text += "\n系统提示:参考音频不存在，无法推理，已自动关闭语音渲染";
		this->user_messages->find(user_id)->second.isOpenVoiceMode = false;
		return;
	}

	CURL *curl = curl_easy_init();
	if (curl)
	{
		// 基础信息配置
		std::string original_url = CManager.configVariable("VIST_API_URL") + ":" + CManager.configVariable("VIST_API_PORT");
		std::string refer_wav_path = CManager.configVariable("VIST_REFERVOICE_PATH");
		std::string prompt_text = CManager.configVariable("VIST_REFERVOICE_TEXT");
		std::string prompt_language = "中文";
		std::string text_language = "中文";

		// 对URL进行URL编码
		std::string url = original_url +
						  "?refer_wav_path=" + encodeToURL(refer_wav_path) +
						  "&prompt_text=" + encodeToURL(prompt_text) +
						  "&prompt_language=" + encodeToURL(prompt_language) +
						  "&text=" + encodeToURL(text) +
						  "&text_language=" + encodeToURL(text_language);

		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &tts_file);

		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK)
		{
			LOG_ERROR("Failed to perform request: " + string(curl_easy_strerror(res)));
			this->user_messages->find(user_id)->second.isOpenVoiceMode = false;
			text += ("\n(系统提示：语音推理失败，管理员可能在维护服务器，已自动关闭语音回复，请稍后再重试...)");
		}
		else
		{
			string URL = "file://" + filePath;
			text = CQCode("record", "file", URL);
		}

		// 清除数据
		tts_file.close();
		curl_easy_cleanup(curl);
	}
}

bool Message::provideImageCreation(const UINT64 user_id, string &text)
{
	// 提取prompt
	string prompt;
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
								   CManager.configVariable("TEXTTRANSLATE_DEFAULT_MODEL"),
								   "EN", CManager.configVariable("TEXTTRANSLATE_MODEL_ENDPOINT"),
								   CManager.configVariable("TEXTTRANSLATE_MODEL_API_KEY"));
	LOG_INFO("文本翻译完成。");
	prompt = JParsingClass.toJson(prompt);
	pair<string, string> p;
	p.first = CManager.configVariable("DRAW_DEFAULT_MODEL");
	p.second = CManager.configVariable("DRAW_DEFUALT_MODEL_SERVICE");
	Dock::RequestGPT(prompt, p, &this->user_messages->find(user_id)->second);

	if (prompt.size() < 100)
	{
		prompt = "网络异常...";
		return false;
	}

	string url;
	if (prompt.rfind("invalid_request_error") != prompt.npos)
	{
		LOG_ERROR("触发官方保护!");
		prompt = prompt.substr(prompt.find("message") + 10); // 前缀分割
		text = prompt.substr(0, prompt.find("request id") - 10);
		url = "";
		LOG_DEBUG(text);
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

		string outputPath;
		/*该代码段是下载模型返回的图片，然后放置指定位置
				LOG_INFO("正在下载图片...");
				// 下载图片
				string filename;
				for (int i = 0; i < 10; i++) // 随机名称
				{
					unsigned short number = rand() % 26 + 65;
					filename += (char)number;
				}

				filename += ".png";
				string inputPath = CManager.configVariable("IMAGE_DOWNLOAD_PATH") + filename;
				outputPath = inputPath;

				// 下载图片
				// 以二进制写入方式打开文件
				std::ofstream outfile(inputPath, std::ios::binary);
				if (!outfile.is_open())
				{
					std::cerr << "Failed to open file for writing" << std::endl;
					return false;
				}

				// 初始化 curl
				CURL *curl = curl_easy_init();
				if (!curl)
				{
					std::cerr << "Failed to initialize curl" << std::endl;
					outfile.close();
					return false;
				}

				// 创建 curl 实例
				curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
				curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback); // write_data
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outfile);

				// 执行下载操作
				CURLcode res = curl_easy_perform(curl);
				if (res != CURLE_OK)
				{
					LOG_ERROR("Failed to download image" + string(curl_easy_strerror(res)));
					outfile.close();
					curl_easy_cleanup(curl);
					return false;
				}

				// 关闭文件
				outfile.close();

				// 清理 curl 实例
				curl_easy_cleanup(curl); // https://www.baidu.com/img/PCtm_d9c8750bed0b3c7d089fa7d55720d6cf.png

		LOG_INFO(outputPath);
		*/

		// 封装格式(采用RUL)
		outputPath = url;
		// 格式封装
		// outputPath.insert(0, "file://"); // 采用绝对路径
		prompt = CQCode("image", "file", outputPath); // 采用网路路径
		text = prompt;
		return true;
	}
}

string Message::removePreviousContext(const UINT64 user_id)
{
	vector<pair<string, time_t>> user_context;
	{
		std::lock_guard<std::mutex> lock(mutex_message);
		user_context = this->user_messages->find(user_id)->second.user_chatHistory;
	}

	if (user_context.size() < 3)
	{
		return string("没有上下文！");
	}

	// 删除最近的上下文
	user_context.pop_back();
	user_context.pop_back();

	// 跟具体数据同步
	std::lock_guard<std::mutex> lock(mutex_message);
	this->user_messages->find(user_id)->second.user_chatHistory = user_context;

	return string("上条对话已被删除！");
}

void Message::SDImageCreation(string &message)
{
	// 提取出prompt
	if (message.size() < 14)
	{
		LOG_WARNING("用户未描述图像");
		message = "系统提示：请描述图像...";
		return;
	}

	string prompt;
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
								   CManager.configVariable("TEXTTRANSLATE_DEFAULT_MODEL"),
								   "EN", CManager.configVariable("TEXTTRANSLATE_MODEL_ENDPOINT"),
								   CManager.configVariable("TEXTTRANSLATE_MODEL_API_KEY"));

	// 调用StableDiffusion
	string base64_code;
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
}