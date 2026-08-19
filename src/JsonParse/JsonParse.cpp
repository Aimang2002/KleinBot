#include "JsonParse.h"
#include "../Log/Log.h"

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
	catch (const std::exception &e)
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
