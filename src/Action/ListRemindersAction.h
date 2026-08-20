#ifndef LIST_REMINDERS_ACTION_H
#define LIST_REMINDERS_ACTION_H

#include "Action.h"
#include "../Reminder/ReminderService.h"

class ListRemindersAction : public Action
{
public:
    explicit ListRemindersAction(ReminderService &reminders) : reminders(reminders) {}

    const ActionDescriptor &descriptor() const override;
    ActionResult execute(const nlohmann::json &arguments,
                         const ActionContext &context) override;

private:
    ReminderService &reminders;
};

#endif // LIST_REMINDERS_ACTION_H
