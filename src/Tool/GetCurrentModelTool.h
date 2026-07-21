#ifndef GET_CURRENT_MODEL_TOOL_H
#define GET_CURRENT_MODEL_TOOL_H

#include "Tool.h"
#include <functional>

class GetCurrentModelTool : public Tool
{
public:
    explicit GetCurrentModelTool(std::function<std::string(uint64_t)> getter)
        : getModelName(std::move(getter)) {}

    std::string name() const override { return "get_current_model"; }

    std::string description() const override
    {
        return "查询当前用户正在使用的聊天模型。用户询问当前模型、正在使用哪个模型时调用。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{}})";
    }

    ToolResult execute(const std::string &, const ToolContext &ctx) override
    {
        return {"当前模型：" + getModelName(ctx.user_id), {}, {}, true};
    }

private:
    std::function<std::string(uint64_t)> getModelName;
};

#endif // GET_CURRENT_MODEL_TOOL_H
