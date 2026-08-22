#ifndef CONFIG_TEMPLATE_H
#define CONFIG_TEMPLATE_H

#include "../../Library/nlohmann/json.hpp"

#include <string>

namespace ConfigTemplate
{
// 生成 32 位十六进制的随机面板访问令牌
std::string generateWebUiToken();

// 内嵌默认配置骨架：必填字段全部为可启动的占位值，webui 启用并绑定回环地址
nlohmann::json defaultDocument(const std::string &webUiToken);

// 配置文件不存在时生成默认配置；返回是否生成，createdToken 输出随机令牌。
// 文件已存在时不动它，生成失败返回 false
bool createIfMissing(const std::string &path, std::string &createdToken);
}

#endif
