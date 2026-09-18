#include "Encoding.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace utils
{
    std::string localToUtf8(const std::string &text)
    {
#if defined(_WIN32)
        if (text.empty())
            return text;
        // 宽松转换：非法字节尽力映射而不是失败，宁要个别乱码不丢整条消息
        const int wideLength = MultiByteToWideChar(
            CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
        if (wideLength <= 0)
            return text;
        std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()),
                            wide.data(), wideLength);

        const int utf8Length = WideCharToMultiByte(
            CP_UTF8, 0, wide.c_str(), wideLength, nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0)
            return text;
        std::string utf8(static_cast<std::size_t>(utf8Length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLength,
                            utf8.data(), utf8Length, nullptr, nullptr);
        while (!utf8.empty() && utf8.back() == '\0')
            utf8.pop_back();
        return utf8;
#else
        return text;
#endif
    }
}
