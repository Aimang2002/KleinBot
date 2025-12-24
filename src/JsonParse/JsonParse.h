#ifndef JSONPARSE_H
#define JSONPARSE_H

#include "../../Library/nlohmann/json.hpp"
#include <iostream>
#include <mutex>

struct JsonData
{
	uint64_t bot_qq;			  // 机器人QQ
	uint64_t user_id;			  // 发送者QQ
	int message_timestamp;		  // 消息时间戳
	int message_id;				  // 消息ID
	int message_seq;			  // 消息序列号（同一用户连续消息相同）
	std::string message_type;	  // 消息类型
	std::string message_data_url; // 有可能有URL
	std::string nickname;		  // 发送者昵称
	std::string raw_message;	  // 原始消息
	std::string sub_type;		  // 子类型
	std::string post_type;		  // 上报类型
	std::string card;			  // ?
	uint64_t group_id = 0;		  // 群号
	std::string type;			  // 发送的文本类型（text为文本类型）
	int error_code;				  // 错误代码
};

class JsonParse
{
public:
	static JsonParse &getInstance();
	JsonData jsonReader(std::string &json_str); // json数据解析
	std::string getAttributeFromChoices(std::string &json_str, std::string Attribute_type);

	/**
	 * @brief 对传入进来的字符串进行转义、去双引号
	 * @param message 需要转义的字符串
	 * @return 返回结果
	 */
	std::string toJson(std::string message);
	void CQCodeSeparation(std::string &message);
	bool findKeyAndValue(const std::string &json, const std::string &key, std::string &value);
	/**
	 * @brief 通用获取 JSON 键值函数
	 * @param data 传入的 json 对象（可以是 json 对象的子项）
	 * @param key 需要寻找的字段名
	 * @return 成功返回字符串，失败（非 JSON、非字符串、字段不存在）返回空字符串
	 */
	std::string getJsonKeyValue(const nlohmann::json &data, const std::string &key);

private:
	JsonParse();
	JsonParse(const JsonParse &) = delete;
	JsonParse &operator=(const JsonParse &) = delete;
	bool findValueByKey(const nlohmann::json &node, const std::string &key, std::string &value);
};

#endif