#include "OneBotEventDecoder.h"

#include "../../Log/Log.h"
#include "../../../Library/nlohmann/json.hpp"

std::optional<InboundMessage> OneBotEventDecoder::decode(const std::string &payload) const
{
    const nlohmann::json document = nlohmann::json::parse(payload);
    if (!document.is_object() || !document.contains("post_type"))
    {
        return std::nullopt;
    }

    const std::string postType = document.value("post_type", "");
    if (postType.empty() || postType == "meta_event")
    {
        return std::nullopt;
    }

    InboundMessage message;
    message.payload_size_bytes = payload.size();
    message.user_id = document.value("user_id", 0ULL);
    if (document.contains("sender") && document["sender"].is_object())
    {
        const auto &sender = document["sender"];
        message.nickname = sender.value("nickname", "");
        message.card = sender.value("card", "");
    }

    message.group_id = document.value("group_id", 0ULL);
    message.message_type = document.value("message_type", "");
    message.post_type = postType;
    message.raw_message = document.value("raw_message", "");

    if (document.contains("message") && document["message"].is_array())
    {
        for (const auto &segment : document["message"])
        {
            const std::string type = segment.value("type", "");
            if (!segment.contains("data") || !segment["data"].is_object())
            {
                continue;
            }

            if (type == "text")
            {
                message.plain_text += segment["data"].value("text", "");
            }
            else if (type == "image")
            {
                message.message_data_url = segment["data"].value("url", "");
            }
        }
    }

    message.message_id = document.value("message_id", 0LL);
    message.message_timestamp = document.value("time", 0LL);
    return message;
}

std::optional<OneBotApiResult> OneBotEventDecoder::decodeResponse(const std::string &payload) const
{
    const nlohmann::json document = nlohmann::json::parse(payload);
    if (!document.is_object() || document.contains("post_type") || !document.contains("echo"))
    {
        return std::nullopt;
    }

    OneBotApiResult result;
    result.echo = document.value("echo", 0LL);
    result.status = document.value("status", "");
    result.retcode = document.value("retcode", 0LL);
    if (document.contains("data"))
    {
        result.data = document["data"];
    }
    return result;
}
