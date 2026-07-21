#ifndef TOOL_CONTEXT_H
#define TOOL_CONTEXT_H

#include <cstdint>

// 工具调用期上下文：承载每次调用才确定的信息（与工具实例无关）
// 工具是全用户共享单例，user_id 每次对话都变，故走调用参数而非构造注入
struct ToolContext
{
    uint64_t user_id = 0;
    int64_t user_message_id = 0;
};

#endif // TOOL_CONTEXT_H
