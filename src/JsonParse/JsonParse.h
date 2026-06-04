#ifndef JSONPARSE_H
#define JSONPARSE_H

#include "../../Library/nlohmann/json.hpp"
#include <iostream>
#include <mutex>

struct JsonData
{
	// 发送者身份
	uint64_t user_id = 0;
	std::string nickname; // QQ 昵称
	std::string card;	  // 群名片

	// 消息归属
	uint64_t group_id = 0;
	std::string message_type; // "group" / "private"
	std::string post_type;	  // "message" / "notice" / "request"

	// 消息内容
	std::string raw_message;	  // 原始消息（含 CQ 码，日志/调试用）
	std::string plain_text;		  // 纯文本（从 message[] 提取，业务用）
	std::string message_data_url; // 图片 URL（从 message[] 中 type=="image" 提取）

	// 消息元数据
	int64_t message_id = 0;
	int64_t message_timestamp = 0;

	// 输出控制（jsonReader 不设置，由 handleMessage 赋值）
	std::string content_type; // "text" / "CQ"
};

class JsonParse
{
public:
	static JsonParse &getInstance();

	JsonData jsonReader(std::string &json_str); // json数据解析

	std::string getAttributeFromChoices(std::string &json_str, std::string Attribute_type);

	std::string toJson(std::string message);

	void CQCodeSeparation(std::string &message);

	bool findKeyAndValue(const std::string &json, const std::string &key, std::string &value);

	std::string getJsonKeyValue(const nlohmann::json &data, const std::string &key);

private:
	JsonParse();
	JsonParse(const JsonParse &) = delete;
	JsonParse &operator=(const JsonParse &) = delete;
	bool findValueByKey(const nlohmann::json &node, const std::string &key, std::string &value);
};

#endif