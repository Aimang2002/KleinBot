#ifndef FEEDBACK_COMMAND_H
#define FEEDBACK_COMMAND_H
/*
 * 人工反馈命令（R0.3）：#好 / #坏，可带备注 "#坏:太正式"（中英文冒号都行）。
 * 引用 Klein 的某条消息回复它时按消息 ID 精确定位；不引用则按最近一次回复兜底。
 * 所有人可用（用户定规 2026-10-28：群友的反馈也是信号，evidence 里记标注人）；
 * 业务在 RecordFeedbackAction
 */
#include "Command.h"
#include "../Action/Action.h"

class FeedbackCommand : public Command
{
public:
    explicit FeedbackCommand(Action &action) : action(action) {}
    bool canHandle(const std::string &message) override;
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override
    {
        return "引用克莱茵的某条回复并发送 #好 或 #坏（可带备注，如 #坏:太正式），记录人工反馈。";
    }

    // 解析 "#好" / "#坏" / "#好:备注" / "#坏：备注"；非反馈命令返回 false
    static bool parse(const std::string &message, std::string &label, std::string &note);

private:
    Action &action;
};

#endif // FEEDBACK_COMMAND_H
