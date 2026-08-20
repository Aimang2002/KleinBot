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

private:
    ReminderService &reminders;
};

#endif // SET_REMINDER_ACTION_H
