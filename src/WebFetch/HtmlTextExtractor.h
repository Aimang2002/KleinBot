#ifndef HTML_TEXT_EXTRACTOR_H
#define HTML_TEXT_EXTRACTOR_H

#include <string>

struct HtmlTextExtraction
{
    std::string title;
    std::string text;
};

// 从 HTML 提取标题与正文文本：丢弃 script/style/nav 等噪声标签，
// 保留标题层级与段落结构，解码常见实体。输入不要求是完整或合法的 HTML；
// 非 UTF-8 内容的多字节字符按原样透传，由上层标记 charset。
HtmlTextExtraction extractHtmlText(const std::string &html);

// 从 HTML 头部探测声明的字符集（返回小写，如 "utf-8"、"gbk"），未声明返回空
std::string detectHtmlCharset(const std::string &html);

#endif // HTML_TEXT_EXTRACTOR_H
