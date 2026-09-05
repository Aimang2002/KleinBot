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

    // notice/request 事件：通用字段之上补齐事件专属字段后直接返回
    if (postType == "notice")
    {
        message.notice_type = document.value("notice_type", "");
        message.sub_type = document.value("sub_type", "");
        message.target_id = document.value("target_id", 0ULL);
        message.operator_id = document.value("operator_id", 0ULL);
        message.message_timestamp = document.value("time", 0LL);
        return message;
    }
    if (postType == "request")
    {
        message.request_type = document.value("request_type", "");
        message.comment = document.value("comment", "");
        message.flag = document.value("flag", "");
        message.message_timestamp = document.value("time", 0LL);
        return message;
    }

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
            else if (type == "at")
            {
                // qq 可能是数字或字符串；"all"（@全体成员）是广播不是点名，不记录。
                // 字段缺失时跳过该段而不是让整个事件解析失败
                if (segment["data"].contains("qq"))
                {
                    const auto &qq = segment["data"]["qq"];
                    if (qq.is_number_unsigned())
                    {
                        message.mentioned_ids.push_back(qq.get<std::uint64_t>());
                    }
                    else if (qq.is_string())
                    {
                        const std::string value = qq.get<std::string>();
                        if (value != "all" && !value.empty() &&
                            value.find_first_not_of("0123456789") == std::string::npos)
                        {
                            message.mentioned_ids.push_back(std::stoull(value));
                        }
                    }
                }
            }
        }
    }

    // message_id 双形态：数字直接取；字符串（NapCat 新版形态）存 raw 供 reply 段回填。
    // 注意不能先 value("message_id", 0LL)——字符串形态会让 value() 抛类型异常丢掉整个事件
    if (document.contains("message_id"))
    {
        if (document["message_id"].is_number_integer())
        {
            message.message_id = document["message_id"].get<std::int64_t>();
        }
        else if (document["message_id"].is_string())
        {
            message.message_id_raw = document["message_id"].get<std::string>();
        }
    }
    if (postType == "message" && message.message_id == 0 && message.message_id_raw.empty())
    {
        // 引用回复依赖此字段；缺失只降级（仅@），但要留痕帮助定位实现端事件差异
        std::string keys;
        for (const auto &item : document.items())
        {
            if (!keys.empty())
                keys += ",";
            keys += item.key();
        }
        LOG_WARNING("入站消息事件缺少 message_id（事件字段：" + keys +
                    "），回应将降级为仅@不引用");
    }
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
