#include "ListRemindersAction.h"
#include "../Application/ReminderToolNames.h"
#include "../Reminder/ReminderTime.h"

namespace
{
std::string repeatLabel(const std::string &repeat)
{
    if (repeat == "daily")
        return "，每天重复";
    if (repeat == "weekly")
        return "，每周重复";
    return "";
}
}

const ActionDescriptor &ListRemindersAction::descriptor() const
{
    static const ActionDescriptor value{
        KleinListRemindersToolName,
        "列出当前用户的全部待触发提醒（编号、触发时间、重复规则和内容）。"
        "用户询问有哪些提醒、待办或定时事项时调用。",
        {{"type", "object"}, {"properties", nlohmann::json::object()}},
        true,
        false};
    return value;
}

ActionResult ListRemindersAction::execute(const nlohmann::json & /*arguments*/,
                                          const ActionContext &context)
{
    const auto pending = reminders.list(context.user_id);
    if (pending.empty())
        return {"当前没有待触发的提醒。", {}, {}, false};
    std::string output = "当前共有 " + std::to_string(pending.size()) + " 条提醒：";
    for (const ReminderRecord &record : pending)
        output += "\n编号 " + std::to_string(record.id) + "：" +
                  formatLocal(record.trigger_at) + repeatLabel(record.repeat_rule) +
                  "：" + record.content;
    return {output, {}, {}, false};
}
