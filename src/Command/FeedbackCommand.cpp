#include "FeedbackCommand.h"
#include "../utils/Utils.hpp"

bool FeedbackCommand::parse(const std::string &message, std::string &label, std::string &note)
{
    const std::string text = utils::trim(message);
    static const char *const kGood = "#好";
    static const char *const kBad = "#坏";
    const char *head = nullptr;
    if (text.rfind(kGood, 0) == 0)
    {
        head = kGood;
        label = "good";
    }
    else if (text.rfind(kBad, 0) == 0)
    {
        head = kBad;
        label = "bad";
    }
    else
    {
        return false;
    }

    std::string rest = text.substr(std::string(head).size());
    if (rest.empty())
    {
        note.clear();
        return true;
    }
    // 只接受冒号分隔的备注；"#好吃" 这类普通聊天不是命令
    static const std::string kHalfColon = ":";
    static const std::string kFullColon = "：";
    if (rest.rfind(kHalfColon, 0) == 0)
        rest = rest.substr(kHalfColon.size());
    else if (rest.rfind(kFullColon, 0) == 0)
        rest = rest.substr(kFullColon.size());
    else
        return false;
    note = utils::trim(rest);
    return true;
}

bool FeedbackCommand::canHandle(const std::string &message)
{
    std::string label;
    std::string note;
    return parse(message, label, note);
}

CommandResult FeedbackCommand::execute(const CommandContext &ctx)
{
    std::string label;
    std::string note;
    if (!parse(ctx.data.plain_text, label, note))
        return {TextMessage{"用法：引用她的回复后发送 #好 或 #坏（可带备注，如 #坏:太正式）。"}};

    nlohmann::json arguments;
    arguments["label"] = label;
    arguments["note"] = note;
    // 引用的消息 ID：字符串形态优先（个别实现端），否则数字转字符串；无引用为空
    arguments["reply_to_message_id"] =
        !ctx.data.reply_to_message_id_raw.empty()
            ? ctx.data.reply_to_message_id_raw
            : (ctx.data.reply_to_message_id != 0 ? std::to_string(ctx.data.reply_to_message_id)
                                                 : std::string());
    arguments["group_id"] = ctx.message_type == "group" ? ctx.group_id : 0;
    const auto result = action.execute(arguments, {ctx.user_id, ctx.data.message_id, ctx.data.plain_text});
    return {TextMessage{result.content}};
}
