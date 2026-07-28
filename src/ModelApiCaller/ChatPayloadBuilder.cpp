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
        payload["system"] = request.system_prompt;

    nlohmann::json messages = nlohmann::json::array();
    for (const ChatMessage &message : request.history)
    {
        if (message.role == "system")
            continue;
        messages.push_back({
            {"role", message.role},
            {"content", anthropicContent(message)}
        });
    }
    payload["messages"] = std::move(messages);
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
