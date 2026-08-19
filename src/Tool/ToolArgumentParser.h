#ifndef TOOL_ARGUMENT_PARSER_H
#define TOOL_ARGUMENT_PARSER_H

#include "../../Library/nlohmann/json.hpp"
#include <string>

nlohmann::json parseToolArguments(const std::string &arguments);

// 把网关返回的畸形参数串（如两个拼接的 JSON 对象）归一化为合法 JSON 文本；
// 解析失败时原样返回，保证回灌给网关的内容不会比收到的更差
std::string normalizeToolArguments(const std::string &arguments);

#endif // TOOL_ARGUMENT_PARSER_H
