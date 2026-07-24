#ifndef JSONPARSE_H
#define JSONPARSE_H

#include "../../Library/nlohmann/json.hpp"
#include <iostream>
#include <mutex>

class JsonParse
{
public:
	static JsonParse &getInstance();

	std::string getAttributeFromChoices(std::string &json_str, std::string Attribute_type);

	std::string toJson(std::string message);

	bool findKeyAndValue(const std::string &json, const std::string &key, std::string &value);

	std::string getJsonKeyValue(const nlohmann::json &data, const std::string &key);

private:
	JsonParse();
	JsonParse(const JsonParse &) = delete;
	JsonParse &operator=(const JsonParse &) = delete;
	bool findValueByKey(const nlohmann::json &node, const std::string &key, std::string &value);
};

#endif
