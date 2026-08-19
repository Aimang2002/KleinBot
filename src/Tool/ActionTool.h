#ifndef ACTION_TOOL_H
#define ACTION_TOOL_H

#include "Tool.h"
#include "ToolArgumentParser.h"
#include "../Action/Action.h"

class ActionTool : public Tool
{
public:
    explicit ActionTool(Action &action) : action(action) {}

    std::string name() const override { return action.descriptor().name; }
    std::string description() const override { return action.descriptor().description; }
    std::string parametersSchema() const override
    {
        return action.descriptor().parameters_schema.dump();
    }
    bool requiresAdmin() const override { return action.descriptor().requires_admin; }

    ToolResult execute(const std::string &args, const ToolContext &context) override
    {
        try
        {
            const auto result = action.execute(parseToolArguments(args),
                                               {context.user_id, context.user_message_id,
                                                context.user_text});
            return {result.content, result.outbound_messages, result.context_content,
                    result.terminal, false, false};
        }
        catch (const std::exception &error)
        {
            return {"错误：操作参数无效：" + std::string(error.what()), {}, {}, true};
        }
    }

private:
    Action &action;
};

#endif // ACTION_TOOL_H
