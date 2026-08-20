#include "CancelReminderAction.h"
#include "../Application/ReminderToolNames.h"

const ActionDescriptor &CancelReminderAction::descriptor() const
{
    static const ActionDescriptor value{
        KleinCancelReminderToolName,
        "取消当前用户的一条提醒。用户要求取消、删除提醒时调用；"
        "不知道编号时先调用 list_reminders 查询。",
        {{"type", "object"},
         {"properties",
          {{"id",
            {{"type", "integer"},
             {"description", "要取消的提醒编号，来自 list_reminders 的结果"}}}}},
         {"required", {"id"}},
         {"additionalProperties", false}},
        true,
        false};
    return value;
}

ActionResult CancelReminderAction::execute(const nlohmann::json &arguments,
                                           const ActionContext &context)
{
    if (!arguments.is_object())
        return {"错误：参数必须是对象。", {}, {}, false};
    const auto idValue = arguments.find("id");
    if (idValue == arguments.end() || !idValue->is_number_integer())
        return {"错误：id 必须是整数，可先用 list_reminders 查询编号。", {}, {}, false};
    const int64_t id = idValue->get<int64_t>();
    if (!reminders.cancel(context.user_id, id))
        return {"未找到编号 " + std::to_string(id) + " 的提醒，可用 list_reminders 核对编号。", {}, {}, false};
    return {"提醒已取消（编号 " + std::to_string(id) + "）。", {}, {}, false};
}
