#ifndef TEXT_SPLIT_H
#define TEXT_SPLIT_H

#include <cstddef>
#include <string>
#include <vector>

// 把模型输出切分成"一条消息一段"的发送序列（真人一句一条的打字习惯）：
// - 单个换行 = 分条边界；空白行丢弃
// - ``` 代码块整体保持为一条消息（内部换行不拆），未闭合时按已积累内容发送
// - 段数上限 maxSegments：超出部分以换行并入最后一段（防刷屏）
// - 单段超过 maxChars 按 UTF-8 字符硬切（防协议截断到半个字符）
std::vector<std::string> splitTextSegments(const std::string &text,
                                           std::size_t maxChars,
                                           std::size_t maxSegments = 10);

// 整段原样单发（私聊路径）：换行不做分条边界，仅超过 maxChars 时
// 按 UTF-8 字符硬切兜底（防协议截断到半个字符）；空文本返回空序列
std::vector<std::string> splitTextWhole(const std::string &text,
                                        std::size_t maxChars);

#endif // TEXT_SPLIT_H
