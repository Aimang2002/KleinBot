#ifndef ENCODING_H
#define ENCODING_H

#include <string>

namespace utils
{
    // 把 Windows 本地 ANSI 代码页文本（如系统错误消息，中文系统为 GBK）转为 UTF-8，
    // 供日志与文件落盘使用；非 Windows 平台原样返回。
    // 只对系统/第三方库返回的原始消息调用——勿在拼接了自身 UTF-8 文本后再整体转换
    std::string localToUtf8(const std::string &text);
}

#endif // ENCODING_H
