#include "MemoryExtractor.h"
#include "../Log/Log.h"
#include "../../Library/nlohmann/json.hpp"
#include <algorithm>

namespace
{
double clampScore(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

std::string jsonPayload(const std::string &content)
{
    const std::size_t start = content.find('{');
    const std::size_t end = content.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end < start)
        return {};
    return content.substr(start, end - start + 1);
}
}

std::vector<MemoryMutation> MemoryExtractor::extract(uint64_t user_id,
                                                     const std::vector<MemoryTurn> &turns)
{
    if (turns.empty())
        return {};

    const std::optional<ChatModel> model = models.find(modelName);
    if (!model)
    {
        LOG_ERROR("长期记忆模型未注册：" + modelName);
        return {};
    }

    nlohmann::json conversation = nlohmann::json::array();
    int64_t sourceStartId = 0;
    int64_t sourceEndId = 0;
    for (const auto &turn : turns)
    {
        conversation.push_back({{"user", turn.user_text}, {"assistant", turn.assistant_text}});
        if (sourceStartId == 0 || (turn.source_start_id > 0 && turn.source_start_id < sourceStartId))
            sourceStartId = turn.source_start_id;
        sourceEndId = std::max(sourceEndId, turn.source_end_id);
    }

    ChatRequest request;
    request.temperature = 0.1;
    request.max_tokens = 1600;
    request.system_prompt =
        "你是长期记忆提取器。只提取未来对话仍可能有价值、且能从原文直接确认的信息。"
        "允许类型：profile、preference、relationship、event、state、decision、task、technical。"
        "忽略寒暄、一次性问题、助手推测和无法确认的信息。"
        "memory_key 必须稳定且简短，例如 preference.favorite_game、profile.city；同一事实变化时复用相同 key。"
        "事件类 key 应包含日期或唯一短标识，避免不同事件相互覆盖。"
        "search_text 要包含规范事实以及原文可能使用的同义表达和关键词，以空格分隔。"
        "对于可表示为实体当前属性的资料、偏好、关系状态、项目配置或设备信息，额外输出："
        "subject_key、subject_type、subject_name、subject_aliases、predicate、value_text。"
        "subject_key 必须是稳定且可读的实体标识，例如 user:self、project:kleinbot、person:林安；"
        "predicate 使用稳定的 snake_case 属性名；value_text 保存开放文本值，不要限制为枚举。"
        "复杂事件、长经历或无法稳定拆成实体属性的记忆可以省略这些结构化字段。"
        "若用户明确撤销或要求遗忘某项事实，action 使用 delete，否则使用 upsert。"
        "只输出 JSON，不要输出 Markdown。格式："
        R"({"memories":[{"action":"upsert","memory_key":"...","memory_type":"preference","canonical_text":"...","search_text":"...","subject_key":"user:self","subject_type":"user","subject_name":"用户本人","subject_aliases":["我","用户"],"predicate":"favorite_game","value_text":"塞尔达传说","importance":0.0,"confidence":0.0}]})";
    request.history.push_back({"user", "用户ID仅用于隔离，不要写入记忆：" + std::to_string(user_id) +
                                           "\n待分析对话：" + conversation.dump()});

    ChatResponse response = dock.RequestChat(*model, modelName, request);
    if (response.cancelled)
        return {};
    if (response.code != 200 || response.content.empty())
    {
        LOG_WARNING("长期记忆提取失败，模型返回码：" + std::to_string(response.code));
        return {};
    }

    std::vector<MemoryMutation> mutations;
    try
    {
        nlohmann::json root = nlohmann::json::parse(jsonPayload(response.content));
        if (!root.contains("memories") || !root["memories"].is_array())
            return mutations;

        for (const auto &entry : root["memories"])
        {
            if (!entry.is_object())
                continue;
            MemoryMutation mutation;
            mutation.action = entry.value("action", "upsert");
            mutation.item.user_id = user_id;
            mutation.item.memory_key = entry.value("memory_key", "");
            mutation.item.memory_type = entry.value("memory_type", "event");
            mutation.item.canonical_text = entry.value("canonical_text", "");
            mutation.item.search_text = entry.value("search_text", mutation.item.canonical_text);
            mutation.item.subject_key = entry.value("subject_key", "");
            mutation.item.subject_type = entry.value("subject_type", "");
            mutation.item.subject_name = entry.value("subject_name", "");
            mutation.item.predicate = entry.value("predicate", "");
            mutation.item.value_text = entry.value("value_text", "");
            if (entry.contains("subject_aliases") && entry["subject_aliases"].is_array())
            {
                for (const auto &alias : entry["subject_aliases"])
                {
                    if (alias.is_string() && !alias.get<std::string>().empty())
                        mutation.item.subject_aliases.push_back(alias.get<std::string>());
                }
            }
            mutation.item.importance = clampScore(entry.value("importance", 0.5));
            mutation.item.confidence = clampScore(entry.value("confidence", 0.5));
            mutation.item.source_start_id = sourceStartId;
            mutation.item.source_end_id = sourceEndId;

            const bool hasStructuredField = !mutation.item.subject_key.empty() ||
                                            !mutation.item.predicate.empty() ||
                                            !mutation.item.value_text.empty();
            const bool hasCompleteStructuredFact = !mutation.item.subject_key.empty() &&
                                                   !mutation.item.predicate.empty() &&
                                                   !mutation.item.value_text.empty();
            if (hasStructuredField && !hasCompleteStructuredFact)
            {
                mutation.item.subject_key.clear();
                mutation.item.subject_type.clear();
                mutation.item.subject_name.clear();
                mutation.item.subject_aliases.clear();
                mutation.item.predicate.clear();
                mutation.item.value_text.clear();
            }
            if (mutation.item.memory_key.empty() && hasCompleteStructuredFact)
                mutation.item.memory_key = mutation.item.subject_key + "." + mutation.item.predicate;
            if (mutation.item.memory_key.empty())
                continue;
            if (mutation.action != "delete" && mutation.item.canonical_text.empty())
                continue;
            mutations.push_back(std::move(mutation));
        }
    }
    catch (const std::exception &error)
    {
        LOG_ERROR("长期记忆 JSON 解析失败：" + std::string(error.what()));
    }
    return mutations;
}
