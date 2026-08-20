#ifndef CANCEL_REMINDER_ACTION_H
#define CANCEL_REMINDER_ACTION_H

#include "Action.h"
#include "../Reminder/ReminderService.h"

class CancelReminderAction : public Action
{
public:
    explicit CancelReminderAction(ReminderService &reminders) : reminders(reminders) {}

    const ActionDescriptor &descriptor() const override;
    ActionResult execute(const nlohmann::json &arguments,
                         const ActionContext &context) override;

private:
    ReminderService &reminders;
};

#endif // CANCEL_REMINDER_ACTION_H
