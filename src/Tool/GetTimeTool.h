#ifndef GET_TIME_TOOL_H
#define GET_TIME_TOOL_H

#include "Tool.h"
#include <ctime>

// 最小工具：返回当前时间，无参数、无副作用、瞬时可验证
class GetTimeTool : public Tool
{
public:
    std::string name() const override { return "get_time"; }

    std::string description() const override
    {
        return "获取服务器当前的日期和时间。当用户询问现在几点、今天日期等时间相关问题时调用。";
    }

    std::string parametersSchema() const override
    {
        // 无参数：空 properties 的 object schema
        return R"({"type":"object","properties":{}})";
    }

    std::string execute(const std::string & /*args*/, const ToolContext & /*ctx*/) override
    {
        std::time_t now = std::time(nullptr);
        std::tm *local = std::localtime(&now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", local);
        return std::string(buf);
    }
};

#endif // GET_TIME_TOOL_H
