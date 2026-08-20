#include "SetReminderAction.h"
#include "../Application/ReminderToolNames.h"
#include "../Reminder/ReminderTime.h"
#include <algorithm>

namespace
{
std::string trim(std::string value)
{
    const auto notSpace = [](unsigned char character) { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string repeatLabel(const std::string &repeat)
{
    if (repeat == "daily")
        return "，每天重复";
    if (repeat == "weekly")
        return "，每周重复";
    return "";
}
}

const ActionDescriptor &SetReminderAction::descriptor() const
{
    static const ActionDescriptor value{
        KleinSetReminderToolName,
        "为当前用户注册一条定时提醒，到点后 Klein 会私聊转达。"
        "用户说“提醒我”“到点叫我”“别忘了”等定时事项时调用。"
        "相对时间（明天、下周三）必须先换算成本地时区的具体日期时间。",
        {{"type", "object"},
         {"properties",
          {{"content",
            {{"type", "string"},
             {"description", "提醒内容：到点要转达给用户的事项，一句自然的中文"}}},
           {"time",
            {{"type", "string"},
             {"description", "触发时间，本地时区 ISO 格式 YYYY-MM-DDTHH:MM，例如 2026-08-20T09:30"}}},
           {"repeat",
            {{"type", "string"},
             {"enum", {"none", "daily", "weekly"}},
             {"description", "重复规则：一次性不传或传 none，每天/每周提醒分别传 daily/weekly"}}}}},
         {"required", {"content", "time"}},
         {"additionalProperties", false}},
        true,
        false};
    return value;
}

ActionResult SetReminderAction::execute(const nlohmann::json &arguments,
                                        const ActionContext &context)
{
    if (!arguments.is_object())
        return {"错误：参数必须是对象。", {}, {}, false};

    const auto contentValue = arguments.find("content");
    if (contentValue == arguments.end() || !contentValue->is_string())
        return {"错误：content 必须是非空字符串。", {}, {}, false};
    const std::string content = trim(contentValue->get<std::string>());
    if (content.empty())
        return {"错误：content 必须是非空字符串。", {}, {}, false};
    // UTF-8 下 1 汉字 ≈ 3 字节，1500 字节约等于 500 字
    if (content.size() > 1500)
        return {"错误：提醒内容过长，请精简到 500 字以内。", {}, {}, false};

    const auto timeValue = arguments.find("time");
    if (timeValue == arguments.end() || !timeValue->is_string())
        return {"错误：time 必须是 YYYY-MM-DDTHH:MM 格式的本地时间字符串。", {}, {}, false};
    const auto triggerAt = parseIsoLocal(timeValue->get<std::string>());
    if (!triggerAt.has_value())
        return {"错误：time 格式无效，应为本地时区 YYYY-MM-DDTHH:MM（例如 2026-08-20T09:30）。", {}, {}, false};
    if (*triggerAt <= nowSeconds())
        return {"错误：触发时间必须晚于当前时间，请确认日期没有写错。", {}, {}, false};

    std::string repeat = "none";
    const auto repeatValue = arguments.find("repeat");
    if (repeatValue != arguments.end())
    {
        if (!repeatValue->is_string())
            return {"错误：repeat 只能是 none、daily 或 weekly。", {}, {}, false};
        repeat = repeatValue->get<std::string>();
        if (repeat != "none" && repeat != "daily" && repeat != "weekly")
            return {"错误：repeat 只能是 none、daily 或 weekly。", {}, {}, false};
    }

    if (reminders.pendingCount(context.user_id) >= ReminderService::kMaxPendingPerUser)
        return {"错误：当前用户的待触发提醒已达上限（" +
                    std::to_string(ReminderService::kMaxPendingPerUser) +
                    " 条），请先取消部分提醒后再设置。", {}, {}, false};

    const int64_t id = reminders.add(context.user_id, content, *triggerAt, repeat);
    if (id == 0)
        return {"错误：提醒写入失败，请稍后重试。", {}, {}, false};
    return {"提醒已设置：" + formatLocal(*triggerAt) + repeatLabel(repeat) +
                "，编号 " + std::to_string(id) + "。请向用户复述确认这条安排。", {}, {}, false};
}
