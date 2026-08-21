#include "ChatPayloadBuilder.h"

#include <algorithm>
#include <cctype>

namespace
{
nlohmann::json openAIContent(const ChatMessage &message)
{
    if (message.images.empty())
        return message.content;

    nlohmann::json content = nlohmann::json::array();
    if (!message.content.empty())
        content.push_back({{"type", "text"}, {"text", message.content}});
    for (const ChatImageContent &image : message.images)
    {
        content.push_back({
            {"type", "image_url"},
            {"image_url", {{"url", "data:" + image.mime_type + ";base64," + image.base64_data}}}
        });
    }
    return content;
}

nlohmann::json anthropicContent(const ChatMessage &message)
{
    if (message.images.empty())
        return message.content;

    nlohmann::json content = nlohmann::json::array();
    for (const ChatImageContent &image : message.images)
    {
        content.push_back({
            {"type", "image"},
            {"source", {
                {"type", "base64"},
                {"media_type", image.mime_type},
                {"data", image.base64_data}
            }}
        });
    }
    if (!message.content.empty())
        content.push_back({{"type", "text"}, {"text", message.content}});
    return content;
}

// 在消息的最后一个内容块上挂 cache_control 断点。
// 断点只能挂在块上，纯文本消息需先转为块数组；两种编码对模型输入等价
void attachCacheBreakpoint(nlohmann::json &message)
{
    if (!message.contains("content"))
        return;
    if (message["content"].is_string())
    {
        const std::string text = message["content"];
        message["content"] = nlohmann::json::array(
            {{{"type", "text"}, {"text", text}}});
    }
    if (message["content"].is_array() && !message["content"].empty())
        message["content"].back()["cache_control"] = {{"type", "ephemeral"}};
}
}

nlohmann::json ChatPayloadBuilder::openAI(const std::string &modelName,
                                          const ChatRequest &request)
{
    nlohmann::json payload;
    payload["model"] = modelName;
    payload["temperature"] = request.temperature;
    payload["frequency_penalty"] = request.frequency_penalty;
    payload["presence_penalty"] = request.presence_penalty;
    nlohmann::json messages = nlohmann::json::array();

    if (!request.system_prompt.empty())
        messages.push_back({{"role", "system"}, {"content", request.system_prompt}});

    for (const ChatMessage &message : request.history)
    {
        nlohmann::json encoded;
        encoded["role"] = message.role;
        if (message.role == "assistant" && !message.tool_calls.empty())
        {
            encoded["content"] = message.content.empty()
                ? nlohmann::json(nullptr)
                : nlohmann::json(message.content);
            nlohmann::json calls = nlohmann::json::array();
            for (const ToolCallRequest &call : message.tool_calls)
            {
                calls.push_back({
                    {"id", call.id},
                    {"type", "function"},
                    {"function", {{"name", call.name}, {"arguments", call.arguments}}}
                });
            }
            encoded["tool_calls"] = calls;
        }
        else if (message.role == "tool")
        {
            encoded["content"] = message.content;
            encoded["tool_call_id"] = message.tool_call_id;
        }
        else
        {
            encoded["content"] = openAIContent(message);
        }
        messages.push_back(std::move(encoded));
    }
    payload["messages"] = std::move(messages);

    if (!request.tools.empty())
    {
        nlohmann::json tools = nlohmann::json::array();
        for (const std::string &schema : request.tools)
            tools.push_back(nlohmann::json::parse(schema));
        payload["tools"] = std::move(tools);
    }
    return payload;
}

nlohmann::json ChatPayloadBuilder::anthropic(const std::string &modelName,
                                             const ChatRequest &request,
                                             int defaultMaxTokens)
{
    nlohmann::json payload;
    payload["model"] = modelName;
    payload["temperature"] = request.temperature;
    payload["max_tokens"] = request.max_tokens > 0
        ? request.max_tokens
        : defaultMaxTokens;
    if (!request.system_prompt.empty())
    {
        // 协议的提示缓存必须显式打 cache_control 断点，且最多 4 个；
        // 布局为 tools 末项、system、倒数第 3 条消息、最后一条消息
        payload["system"] = nlohmann::json::array({
            {{"type", "text"}, {"text", request.system_prompt},
             {"cache_control", {{"type", "ephemeral"}}}}
        });
    }

    nlohmann::json messages = nlohmann::json::array();
    for (std::size_t i = 0; i < request.history.size(); ++i)
    {
        const ChatMessage &message = request.history[i];
        if (message.role == "system")
            continue;

        // Anthropic 协议要求 tool_use 的结果作为下一条 user 消息里的 tool_result 块
        // 回传，同一轮的多个结果必须合并在同一条消息中，因此聚合连续的 tool 消息
        if (message.role == "tool")
        {
            nlohmann::json results = nlohmann::json::array();
            while (i < request.history.size() && request.history[i].role == "tool")
            {
                results.push_back({
                    {"type", "tool_result"},
                    {"tool_use_id", request.history[i].tool_call_id},
                    {"content", request.history[i].content}
                });
                ++i;
            }
            --i;
            messages.push_back({{"role", "user"}, {"content", std::move(results)}});
            continue;
        }

        if (message.role == "assistant" && !message.tool_calls.empty())
        {
            nlohmann::json content = nlohmann::json::array();
            if (!message.content.empty())
                content.push_back({{"type", "text"}, {"text", message.content}});
            for (const ToolCallRequest &call : message.tool_calls)
            {
                nlohmann::json input = nlohmann::json::object();
                if (!call.arguments.empty())
                {
                    // 部分网关会拼接损坏的参数文本，解析失败时退回空对象，
                    // 避免整条 tool_use 块无法序列化导致请求被网关拒绝
                    try
                    {
                        input = nlohmann::json::parse(call.arguments);
                    }
                    catch (const nlohmann::json::exception &)
                    {
                    }
                }
                content.push_back({
                    {"type", "tool_use"},
                    {"id", call.id},
                    {"name", call.name},
                    {"input", std::move(input)}
                });
            }
            messages.push_back({{"role", "assistant"}, {"content", std::move(content)}});
            continue;
        }

        messages.push_back({
            {"role", message.role},
            {"content", anthropicContent(message)}
        });
    }
    // Anthropic 要求 user/assistant 角色交替；工具结果聚合成的 user 消息
    // 之后若紧跟普通 user 消息（如工具轮次上限的收尾注记），把后者的
    // 内容块并入前一条 user 消息，避免请求被协议拒绝
    for (std::size_t i = 1; i < messages.size(); )
    {
        if (messages[i].value("role", "") == "user" &&
            messages[i - 1].value("role", "") == "user")
        {
            nlohmann::json &target = messages[i - 1]["content"];
            if (target.is_string())
            {
                const std::string text = target;
                target = nlohmann::json::array(
                    {{{"type", "text"}, {"text", text}}});
            }
            const nlohmann::json &source = messages[i]["content"];
            if (source.is_string())
            {
                target.push_back({{"type", "text"}, {"text", source.get<std::string>()}});
            }
            else if (source.is_array())
            {
                for (const auto &block : source)
                    target.push_back(block);
            }
            messages.erase(messages.begin() + static_cast<long>(i));
        }
        else
        {
            ++i;
        }
    }

    // 工具循环每轮固定追加 2 条消息（assistant tool_use + user tool_result），
    // 跨轮次同样追加 2 条（assistant 回复 + 新 user），因此上一请求的末条
    // 断点恰好落在本请求的倒数第 3 条：该断点负责命中上一请求写入的缓存，
    // 末条断点负责把新增后缀写入缓存
    if (messages.size() >= 3)
        attachCacheBreakpoint(messages[messages.size() - 3]);
    if (!messages.empty())
        attachCacheBreakpoint(messages.back());
    payload["messages"] = std::move(messages);

    if (!request.tools.empty())
    {
        nlohmann::json tools = nlohmann::json::array();
        for (const std::string &schema : request.tools)
        {
            const nlohmann::json definition = nlohmann::json::parse(schema);
            const nlohmann::json function = definition.value(
                "function", nlohmann::json::object());
            tools.push_back({
                {"name", function.value("name", "")},
                {"description", function.value("description", "")},
                {"input_schema", function.value("parameters", nlohmann::json::object())}
            });
        }
        // tools 位于缓存前缀最前，单独断点保证 system 变化时工具表仍可命中
        tools.back()["cache_control"] = {{"type", "ephemeral"}};
        payload["tools"] = std::move(tools);
    }
    return payload;
}

std::size_t ChatPayloadBuilder::imageCount(const ChatRequest &request)
{
    std::size_t count = 0;
    for (const ChatMessage &message : request.history)
        count += message.images.size();
    return count;
}

bool ChatPayloadBuilder::explicitlyRejectsMultimodal(
    long statusCode, const std::string &responseBody)
{
    if (statusCode < 400 || statusCode >= 500 || responseBody.empty())
        return false;

    std::string normalized = responseBody;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });

    if (normalized.find("messages.content.type") != std::string::npos &&
        normalized.find("['text']") != std::string::npos)
        return true;

    const bool unsupported =
        normalized.find("unsupported") != std::string::npos ||
        normalized.find("not support") != std::string::npos ||
        normalized.find("不支持") != std::string::npos ||
        normalized.find("仅支持文本") != std::string::npos;
    const bool mentionsMultimodal =
        normalized.find("image_url") != std::string::npos ||
        normalized.find("multimodal") != std::string::npos ||
        normalized.find("图片") != std::string::npos;
    return unsupported && mentionsMultimodal;
}
