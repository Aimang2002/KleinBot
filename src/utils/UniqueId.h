#ifndef UTILS_UNIQUE_ID_H
#define UTILS_UNIQUE_ID_H

#include <string>

namespace utils
{
// 进程内唯一、带前缀的可读 ID：<prefix>_<毫秒时间戳hex>_<自增>_<随机64位hex>。
// affect 的 decision/execution/feedback ID 与出站投递的 correlation_id 共用，
// 这样"执行记录 ID == 投递关联键"不需要任何映射表
std::string uniqueId(const char *prefix);
}

#endif // UTILS_UNIQUE_ID_H
