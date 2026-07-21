#ifndef RECALL_CONVERSATION_TOOL_H
#define RECALL_CONVERSATION_TOOL_H

#include "Tool.h"
#include "../Memory/MemoryService.h"
#include "../../Library/nlohmann/json.hpp"
#include <string>
#include <vector>

// 召回工具：优先检索结构化长期记忆，未命中时回退原始历史。
class RecallConversationTool : public Tool
{
public:
    explicit RecallConversationTool(MemoryService &memoryService) : memoryService(memoryService) {}

    std::string name() const override { return "recall_conversation"; }

    std::string description() const override
    {
        return "检索当前用户的长期记忆和原始历史。当用户询问以前说过的资料、偏好、经历、"
               "决定或长期状态时调用。请生成1到5个可能出现在历史中的同义检索短语，"
               "不要只复述用户问题中的单个词。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"queries":{"type":"array","items":{"type":"string"},"minItems":1,"maxItems":5,"description":"同义或相关的检索短语，例如失眠、睡不着、睡眠问题"},"limit":{"type":"integer","minimum":1,"maximum":20,"description":"最多返回的记忆条数"}},"required":["queries"]})";
    }

    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        std::vector<std::string> queries;
        std::size_t limit = 8;
        try
        {
            auto j = nlohmann::json::parse(args);
            if (j.contains("queries") && j["queries"].is_array())
            {
                for (const auto &query : j["queries"])
                {
                    if (query.is_string() && !query.get<std::string>().empty())
                        queries.push_back(query.get<std::string>());
                    if (queries.size() >= 5)
                        break;
                }
            }
            const int requestedLimit = j.value("limit", 8);
            if (requestedLimit > 0)
                limit = static_cast<std::size_t>(requestedLimit);
        }
        catch (const std::exception &)
        {
            return {"错误：参数解析失败，请提供 queries 数组。", {}, {}};
        }
        if (queries.empty())
            return {"错误：queries 不能为空。", {}, {}};
        return {memoryService.recall(ctx.user_id, queries, limit), {}, {}};
    }

private:
    MemoryService &memoryService;
};

#endif // RECALL_CONVERSATION_TOOL_H
