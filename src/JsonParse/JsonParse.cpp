#include "JsonParse.h"
#include "../Log/Log.h"

// 重载流式运算符
std::ostream &operator<<(std::ostream &os, const JsonData &data)
{
	os << "JsonData {\n";
	os << "  bot_qq: " << data.bot_qq << "\n";
	os << "  user_id: " << data.user_id << "\n";
	os << "  message_timestamp: " << data.message_timestamp << "\n";
	os << "  message_id: " << data.message_id << "\n";
	os << "  message_seq: " << data.message_seq << "\n";
	os << "  message_type: \"" << data.message_type << "\"\n";
	os << "  nickname: \"" << data.nickname << "\"\n";
	os << "  raw_message: \"" << data.raw_message << "\"\n";
	os << "  sub_type: \"" << data.sub_type << "\"\n";
	os << "  post_type: \"" << data.post_type << "\"\n";
	os << "  card: \"" << data.card << "\"\n";
	os << "  group_id: " << data.group_id << "\n";
	os << "  type: \"" << data.type << "\"\n";
	os << "  error_code: " << data.error_code << "\n";
	os << "}";
	return os;
}

std::string JsonParse::getJsonKeyValue(const nlohmann::json &data, const std::string &key)
{
	if (data.is_null())
	{
		return "";
	}

	if (data.is_object())
	{
		if (data.contains(key))
		{
			if (data[key].is_string())
			{
				return data[key].get<std::string>();
			}
			else
			{
				return data[key].dump();
			}
		}

		for (auto it = data.begin(); it != data.end(); ++it)
		{
			std::string result = getJsonKeyValue(it.value(), key);
			if (!result.empty())
			{
				return result;
			}
		}
	}
	else if (data.is_array())
	{
		for (const auto &element : data)
		{
			std::string result = getJsonKeyValue(element, key);
			if (!result.empty())
			{
				return result;
			}
		}
	}

	return "";
}

JsonParse::JsonParse()
{
}

JsonParse &JsonParse::getInstance()
{
	static JsonParse instance;
	return instance;
}

JsonData JsonParse::jsonReader(std::string &json_str)
{
	// 数据提取
	JsonData data;
	nlohmann::json doc = nlohmann::json::parse(json_str);

	// 如果Json解析失败
	if (!doc.is_object())
	{
		std::cout << __LINE__ << ":";
		LOG_ERROR("The JSON string does not contain an object.");
		return {};
	}

#ifdef DEBUG
	/* 输出json数据 */
	for (auto &[key, val] : doc.items())
	{
		std::cout << "Key: " << key << ", Value: ";
		if (val.is_string())
		{
			std::cout << val.get<std::string>() << std::endl;
		}
		else if (val.is_number_integer())
		{
			std::cout << val.get<int64_t>() << std::endl;
		}
		else if (val.is_number_unsigned())
		{
			std::cout << val.get<unsigned int>() << std::endl;
		}
		else
		{
			std::cout << "Unknown data type" << std::endl;
		}
	}
#endif

	// 提取键值对
	if (doc.contains("sender") && doc["sender"].is_object())
	{
		nlohmann::json &sender = doc["sender"];
		data.nickname = sender.value("nickname", "");
		data.card = sender.value("card", "");
	}

	if (doc.contains("message") && doc["message"].is_array() && !doc["message"].empty())
	{
		const auto &firstMsg = doc["message"][0];
		if (firstMsg.contains("type") && firstMsg["type"].is_string())
		{
			data.type = firstMsg["type"].get<std::string>();
		}

		// 如果有URL，则提取
		if (firstMsg.contains("data") && firstMsg["data"].is_object())
		{
			const auto &firstMsg_data = firstMsg["data"];
			data.message_data_url = firstMsg_data.value("url", "");
		}
	}

	data.bot_qq = doc.value("self_id", 0LL);
	data.user_id = doc.value("user_id", 0LL);
	data.message_timestamp = doc.value("time", 0);
	data.message_id = doc.value("message_id", 0);
	data.message_type = doc.value("message_type", "");
	data.message_seq = doc.value("message_seq", 0);
	data.raw_message = doc.value("raw_message", "");
	data.sub_type = doc.value("sub_type", "");
	data.group_id = doc.value("group_id", 0LL);
	data.post_type = doc.value("post_type", "");

	std::cout << data << std::endl;

	return data;
}

std::string JsonParse::getAttributeFromChoices(std::string &json_str, std::string Attribute_type)
{
	nlohmann::json doc = nlohmann::json::parse(json_str);
	if (!doc.is_object())
	{
		std::cout << __LINE__ << ":";
		LOG_ERROR("The JSON string does not contain an object.");
		json_str = "系统提示：没有解析到有效的回复内容，请重新发送！";
		return {};
	}

	// 检查 "choices" 是否存在
	if (doc.contains("choices") && doc["choices"].is_array())
	{
		const nlohmann::json &choicesArray = doc["choices"];
		if (!choicesArray.is_object() || choicesArray.empty())
		{
			return {};
		}

		// 遍历 "choices" 数组
		for (int i = 0; i < choicesArray.size(); i++)
		{
			const nlohmann::json &choice = choicesArray[i]; // 获取对象
			// 检查 "message" 是否存在
			if (choice.contains("message") && choice["message"].is_object())
			{
				const nlohmann::json &messageObject = choice["message"];
				Attribute_type = messageObject.value(Attribute_type, ""); // 找不到返回空
			}
		}
	}
	return std::string();
}

std::string JsonParse::toJson(std::string message)
{
	try
	{
		// 使用nlohmann::json来转义字符串中的特殊字符
		std::string escaped = nlohmann::json(message).dump();
		// 去掉外层的引号，因为调用者会在外面添加引号
		if (!escaped.empty() && escaped[0] == '"' && escaped.back() == '"')
		{
			return escaped.substr(1, escaped.length() - 2);
		}
		return escaped;
	}
	catch (const std::exception& e)
	{
		// 如果转义失败，返回原始字符串（不转义）
		LOG_ERROR("JSON转义失败: " + std::string(e.what()) + ", 输入: " + message);
		return message;
	}
}

bool JsonParse::findValueByKey(const nlohmann::json &node, const std::string &key, std::string &value)
{
	if (node.is_object())
	{
		// 在当前对象中查找
		if (node.contains(key))
		{
			const nlohmann::json &val = node[key.c_str()];
			if (val.is_string())
			{
				value = val.get<std::string>();
				return true;
			}
		}
		// 对每个子对象进行递归查找
		for (auto itr = node.begin(); itr != node.end(); ++itr)
		{
			if (findValueByKey(*itr, key, value))
			{
				return true;
			}
		}
	}
	else if (node.is_array())
	{
		for (int i = 0; i < node.size(); i++)
		{
			if (findValueByKey(node[i], key, value))
			{
				return true;
			}
		}
	}
	return false;
}

bool JsonParse::findKeyAndValue(const std::string &json_str, const std::string &key, std::string &value)
{
	try
	{
		nlohmann::json doc = nlohmann::json::parse(json_str);
		return findValueByKey(doc, key, value);
	}
	catch (const nlohmann::json::parse_error &e)
	{
		LOG_ERROR("Json解析失败: {}" + std::string(e.what()));
		return false;
	}
}

void JsonParse::CQCodeSeparation(std::string &message)
{
	int begin = message.find("[CQ:");
	while (begin != message.npos)
	{
		int i = begin;
		while (message[++i] != ']')
			; // 寻找需要删除的范围
		message.erase(begin, i - begin + 1);
		begin = message.find("[CQ:");
	}
}
