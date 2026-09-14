#ifndef TEXT_TOKENIZE_H
#define TEXT_TOKENIZE_H

/*
 * 文本归一化与切分工具：长期记忆检索（TextRecall）与观察通道
 * 话题热度（PerceptionChannel）共用的基础手法，从 TextRecall
 * 匿名命名空间提升而来，行为零变化。
 */
#include <string>
#include <vector>

namespace utils
{
// 小写化 ASCII、非字母数字折叠为单空格、保留多字节原文；去首尾空白
std::string normalizeText(const std::string &value);

// 按单空格切分（normalizeText 的输出形态）；不含空串
std::vector<std::string> splitWords(const std::string &value);

// 按码点切分 UTF-8（每元素一个字符的字节序列）
std::vector<std::string> utf8Characters(const std::string &value);

// 是否含任意非 ASCII 字节（CJK 判定的粗粒度形态）
bool hasNonAscii(const std::string &value);

// 高频功能词判定（"我们""什么"这类）：召回查询与观察通道热度共用的
// 噪声过滤——它们出现得最多，但从来不是话题
bool isStopTerm(const std::string &term);
}

#endif // TEXT_TOKENIZE_H
