#ifndef GET_CURRENT_MODEL_ACTION_H
#define GET_CURRENT_MODEL_ACTION_H

#include "Action.h"
#include <functional>

class GetCurrentModelAction : public Action
{
public:
    explicit GetCurrentModelAction(std::function<std::string(uint64_t)> getter)
        : getter(std::move(getter)) {}

    const ActionDescriptor &descriptor() const override
    {
        static const ActionDescriptor value{
            "get_current_model",
            "查询当前用户正在使用的聊天模型。用户询问当前模型、正在使用哪个模型时调用。",
            {{"type", "object"}, {"properties", nlohmann::json::object()}},
            true,
            false};
        return value;
    }

    ActionResult execute(const nlohmann::json &, const ActionContext &context) override
    {
        return {"当前模型：" + getter(context.user_id), {}, {}, true};
    }

private:
    std::function<std::string(uint64_t)> getter;
};

#endif // GET_CURRENT_MODEL_ACTION_H
