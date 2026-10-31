#ifndef SET_REMINDER_ACTION_H
#define SET_REMINDER_ACTION_H

#include "Action.h"
#include "../Reminder/ReminderService.h"

class SetReminderAction : public Action
{
public:
    explicit SetReminderAction(ReminderService &reminders) : reminders(reminders) {}

    const ActionDescriptor &descriptor() const override;
    ActionResult execute(const nlohmann::json &arguments,
                         const ActionContext &context) override;
    // 提醒三工具的规则由 set_reminder 承载（list/cancel 总与它一起注册）
    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &context) const override;

private:
    ReminderService &reminders;
};

#endif // SET_REMINDER_ACTION_H
