#ifndef FS_UTIL_H
#define FS_UTIL_H

#include <string>

namespace utils
{
    // 确保文件所在父目录存在（逐级创建）；路径无父目录部分或目录已存在时为无操作。
    // SQLite 打开库文件前必须保证所在目录已存在，否则报 unable to open database file
    // （Windows/POSIX 皆如此），各 Store 构造函数在 sqlite3_open 前调用
    void ensureParentDirectories(const std::string &filePath);
}

#endif // FS_UTIL_H
