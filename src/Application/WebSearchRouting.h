#ifndef WEB_SEARCH_ROUTING_H
#define WEB_SEARCH_ROUTING_H

#include <string>

inline constexpr const char *KleinWebSearchToolName = "klein_web_search";

// 运行机器的本地日期，注入系统提示作为模型的时间锚点
std::string currentLocalDateIso();

#endif // WEB_SEARCH_ROUTING_H
