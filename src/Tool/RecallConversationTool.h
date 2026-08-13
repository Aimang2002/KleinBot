#ifndef RECALL_CONVERSATION_TOOL_H
#define RECALL_CONVERSATION_TOOL_H

#include "Tool.h"
#include "../Memory/MemoryService.h"
#include "../../Library/nlohmann/json.hpp"
#include <string>
#include <vector>

// 召回工具：统一检索结构化事实、开放长期记忆和原始历史。
class RecallConversationTool : public Tool
{
public:
    explicit RecallConversationTool(MemoryService &memoryService) : memoryService(memoryService) {}

    std::string name() const override { return "recall_conversation"; }

    std::string description() const override
    {
        return "检索当前用户的长期记忆和原始历史，并按文本相关性统一排序。"
               "当用户询问以前说过的资料、偏好、经历、决定或长期状态时必须调用。"
               "明确的实体属性问题应同时填写 fact_queries；temporal 可用 current、earliest、"
               "previous 或 timeline。subject 使用 user:self 或可识别的实体名称，predicate 使用"
               "简短 snake_case 属性名。"
               "请生成1到5个包含实体、属性、同义词或原文关键词的检索短语，"
               "不要只复述用户问题中的单个词。没有可靠结果时不要猜测。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"queries":{"type":"array","items":{"type":"string"},"minItems":1,"maxItems":5,"description":"同义或相关的检索短语，例如失眠、睡不着、睡眠问题"},"fact_queries":{"type":"array","maxItems":5,"items":{"type":"object","properties":{"subject":{"type":"string","description":"实体标识或名称，例如 user:self、Klein 项目、林安"},"predicate":{"type":"string","description":"稳定的 snake_case 属性，例如 favorite_game、database、city"},"temporal":{"type":"string","enum":["current","earliest","previous","timeline"]}},"required":["subject","predicate"]}},"limit":{"type":"integer","minimum":1,"maximum":20,"description":"最多返回的记忆条数"}},"required":["queries"]})";
    }

    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        std::vector<std::string> queries;
        std::vector<StructuredFactQuery> factQueries;
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
            if (j.contains("fact_queries") && j["fact_queries"].is_array())
            {
                for (const auto &fact : j["fact_queries"])
                {
                    if (!fact.is_object())
                        continue;
                    StructuredFactQuery query;
                    query.subject = fact.value("subject", "");
                    query.predicate = fact.value("predicate", "");
                    query.temporal = fact.value("temporal", "current");
                    if (!query.subject.empty() && !query.predicate.empty())
                        factQueries.push_back(std::move(query));
                    if (factQueries.size() >= 5)
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
        return {memoryService.recall(
                    ctx.user_id, queries, limit, factQueries, ctx.user_message_id),
                {}, {}};
    }

private:
    MemoryService &memoryService;
};

#endif // RECALL_CONVERSATION_TOOL_H
