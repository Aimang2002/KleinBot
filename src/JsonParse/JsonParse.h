#pragma once
#include <iostream>
#include <mutex>
#include "../../Library/rapidjson/document.h"
#include "../../Library/rapidjson/writer.h"
#include "../../Library/rapidjson/stringbuffer.h"
#include "../../Library/rapidjson/error/en.h"
#include "../Log/Log.h"
typedef unsigned char cs_byte;

using namespace std;
using namespace rapidjson;

typedef long long UINT64;

struct JsonData
{
	string post_type;
	string message;
	string message_type;
	UINT64 private_id = 0;
	UINT64 group_id = 0;
	string nickname;
	int error_code;
};

class JsonParse
{
private:
	static Document doc;
	static std::mutex __mutex;

private:
	bool findValueByKey(const rapidjson::Value &node, const std::string &key, std::string &value);

public:
	JsonParse();
	JsonData jsonReader(std::string &json_str); // json数据解析
	std::string getAttributeFromChoices(std::string &json_str, std::string Attribute_type);
	string toJson(string message);
	void CQCodeSeparation(string &message);
	bool findKeyAndValue(const std::string &json, const std::string &key, std::string &value);
};