#ifndef RECALL_CONVERSATION_TOOL_H
#define RECALL_CONVERSATION_TOOL_H

#include "Tool.h"
#include "../Persistence/ConversationStore.h"
#include "../../Library/nlohmann/json.hpp"
#include <string>

// 召回工具：按关键词检索该用户的历史对话（字面 LIKE）。
// 用于模型回答「很久以前、当前上下文已无」的问题——补足 slicing 够不到的远古历史。
class RecallConversationTool : public Tool
{
public:
    explicit RecallConversationTool(ConversationStore &store) : store(store) {}

    std::string name() const override { return "recall_conversation"; }

    std::string description() const override
    {
        return "按关键词检索与当前用户的历史对话记录。当用户问及很久以前聊过、"
               "但当前对话上下文里已经没有的内容时调用。传入一个能代表要找内容的关键词。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"keyword":{"type":"string","description":"用于检索历史对话的关键词"}},"required":["keyword"]})";
    }

    std::string execute(const std::string &args, const ToolContext &ctx) override
    {
        std::string keyword;
        try
        {
            auto j = nlohmann::json::parse(args);
            keyword = j.value("keyword", "");
        }
        catch (const std::exception &e)
        {
            return "错误：参数解析失败，请提供 keyword 字段。";
        }
        if (keyword.empty())
            return "错误：keyword 不能为空。";

        auto hits = store.search(ctx.user_id, keyword);
        if (hits.empty())
            return "没有找到与「" + keyword + "」相关的历史对话。";

        std::string result = "找到以下相关历史对话：\n";
        for (const auto &m : hits)
        {
            result += "[" + m.role + "] " + m.content + "\n";
        }
        return result;
    }

private:
    ConversationStore &store;
};

#endif // RECALL_CONVERSATION_TOOL_H
