#ifndef LOCAL_DATE_H
#define LOCAL_DATE_H

#include <string>

// 运行机器的本地日期（YYYY-MM-DD）：注入系统提示 / 工具说明作为模型的时间锚点
std::string currentLocalDateIso();

// 运行机器的本地小时（0–23）：决策层情境卡的时段字段
int currentLocalHour();

#endif // LOCAL_DATE_H
