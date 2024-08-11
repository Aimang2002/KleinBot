#include "JsonParse.h"
using namespace rapidjson;

// 静态成员初始化
Document JsonParse::doc = Document();
std::mutex JsonParse::__mutex;

JsonParse::JsonParse()
{
}

JsonData JsonParse::jsonReader(std::string &json_str)
{
	// 数据提取
	JsonData Data;

	this->__mutex.lock();
	doc.Parse(json_str.c_str());

	if (!doc.IsObject())
	{
		std::cerr << "The JSON string does not contain an object." << endl;
		this->__mutex.unlock(); // 提前开锁
		return Data;
	}

#ifdef DEBUG
	/* 输出json数据 */
	for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it)
	{
		std::cout << "Key: " << it->name.GetString() << ", Value: ";
		if (it->value.IsString())
			std::cout << it->value.GetString() << endl;
		else if (it->value.IsInt64())
			std::cout << it->value.GetInt64() << endl;
		else if (it->value.IsUint())
			std::cout << it->value.GetUint() << endl;
		else
			std::cout << "Unknown data type" << endl;
	}
#endif

	for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it)
	{
		if (!strcmp(it->name.GetString(), "post_type"))
			Data.post_type = it->value.GetString();
		else if (!strcmp(it->name.GetString(), "message_type"))
			Data.message_type = it->value.GetString();
		else if (!strcmp(it->name.GetString(), "raw_message"))
			Data.message = it->value.GetString();
		else if (!strcmp(it->name.GetString(), "nickname"))
			Data.nickname = it->value.GetString();
		else if (!strcmp(it->name.GetString(), "user_id"))
		{
			if (it->value.IsInt64())
				Data.private_id = it->value.GetInt64();
			else if (it->value.IsUint())
				Data.private_id = it->value.GetUint();
		}
		else if (!strcmp(it->name.GetString(), "group_id"))
		{
			if (it->value.IsInt64())
				Data.group_id = it->value.GetInt64();
			else if (it->value.IsUint())
				Data.group_id = it->value.GetUint();
		}
	}
	this->__mutex.unlock();
	return Data;
}

std::string JsonParse::getAttributeFromChoices(std::string &json_str, std::string Attribute_type)
{
	this->__mutex.lock();
	doc.Parse(json_str.c_str());
	if (!doc.IsObject())
	{
		std::cerr << "The JSON string does not contain an object." << std::endl;
		this->__mutex.unlock(); // 提前开锁
		json_str = "系统提示：没有解析到有效的回复内容，请重新发送！";
		return std::string();
	}

	// 检查 "choices" 是否存在
	if (doc.HasMember("choices") && doc["choices"].IsArray())
	{
		const rapidjson::Value &choicesArray = doc["choices"].GetArray();

		// 遍历 "choices" 数组
		for (rapidjson::SizeType i = 0; i < choicesArray.Size(); ++i)
		{
			const rapidjson::Value &choice = choicesArray[i];

			// 检查 "message" 是否存在
			if (choice.HasMember("message") && choice["message"].IsObject())
			{
				const rapidjson::Value &messageObject = choice["message"].GetObject();

				// 查找指定属性
				if (messageObject.HasMember(Attribute_type.c_str()) && messageObject[Attribute_type.c_str()].IsString())
				{
					std::string Attribute_value = messageObject[Attribute_type.c_str()].GetString();
					this->__mutex.unlock();
					return Attribute_value;
				}
			}
		}
	}

	this->__mutex.unlock();
	return std::string();
}

string JsonParse::toJson(string message)
{
	/* 将message转为Json格式 */
	if (message.length() > 4)
	{
		int len = message.length();
		std::lock_guard<std::mutex> lock(__mutex);
		this->doc.SetString(message.c_str(), static_cast<rapidjson::SizeType>(message.length()));
		StringBuffer buffer;
		Writer<StringBuffer> writer(buffer);
		this->doc.Accept(writer);
		message = buffer.GetString();
		message.erase(0, 1);
		message.erase(message.size() - 1, 1);
	}
	return message;
}

bool JsonParse::findValueByKey(const rapidjson::Value &node, const std::string &key, std::string &value)
{
	if (node.IsObject())
	{
		// 在当前对象中查找
		if (node.HasMember(key.c_str()))
		{
			const rapidjson::Value &val = node[key.c_str()];
			if (val.IsString())
			{
				value = val.GetString();
				return true;
			}
		}
		// 对每个子对象进行递归查找
		for (auto itr = node.MemberBegin(); itr != node.MemberEnd(); ++itr)
		{
			if (findValueByKey(itr->value, key, value))
			{
				return true;
			}
		}
	}
	else if (node.IsArray())
	{
		for (rapidjson::SizeType i = 0; i < node.Size(); i++)
		{
			if (findValueByKey(node[i], key, value))
			{
				return true;
			}
		}
	}
	return false;
}

bool JsonParse::findKeyAndValue(const std::string &json, const std::string &key, std::string &value)
{
	std::lock_guard<std::mutex> lock(this->__mutex);

	this->doc.Parse(json.c_str());
	if (this->doc.HasParseError())
	{
		LOG_ERROR("Json解析失败");
		return false;
	}

	return findValueByKey(this->doc, key, value);
}

void JsonParse::CQCodeSeparation(string &message)
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
