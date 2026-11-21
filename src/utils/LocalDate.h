#ifndef LOCAL_DATE_H
#define LOCAL_DATE_H

#include <ctime>
#include <string>

// 运行机器的本地时间（线程安全：POSIX 用 localtime_r，Windows 用 localtime_s）。
// 事实卡的日期时刻与群聊材料每行的时刻共用这一处
std::tm localTimeOf(std::time_t value);

// 运行机器的本地日期（YYYY-MM-DD）：注入系统提示 / 工具说明作为模型的时间锚点
std::string currentLocalDateIso();

// 运行机器的本地小时（0–23）：决策层情境卡的时段字段
int currentLocalHour();

// 某时刻所在本地日历日的零点（当天 00:00:00 的时间戳）：冷启动当日帽按这个日界重置
std::time_t localDayStart(std::time_t value);

#endif // LOCAL_DATE_H
