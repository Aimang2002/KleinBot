#include "OneBotEventDecoder.h"

#include "../../Log/Log.h"
#include "../../utils/Utils.hpp"
#include "../../../Library/nlohmann/json.hpp"

namespace
{
// OneBot 实现端（LLOneBot/NapCat 等 JS 系实现）会把数字字段字符串化（JS number
// 精度/序列化差异），标量字段统一宽容提取：期望形态直接取，字符串形态解析，
// 解析不了回退默认值——类型差异绝不允许丢整条事件/响应
const nlohmann::json *fieldOf(const nlohmann::json &document, const char *key)
{
    const auto iterator = document.find(key);
    return iterator != document.end() ? &*iterator : nullptr;
}

std::uint64_t uint64Field(const nlohmann::json &document, const char *key, std::uint64_t fallback)
{
    const nlohmann::json *value = fieldOf(document, key);
    if (value == nullptr)
        return fallback;
    if (value->is_number_unsigned())
        return value->get<std::uint64_t>();
    if (value->is_number_integer())
    {
        const std::int64_t signedValue = value->get<std::int64_t>();
        return signedValue >= 0 ? static_cast<std::uint64_t>(signedValue) : fallback;
    }
    if (value->is_number_float())
        return value->get<double>() >= 0 ? static_cast<std::uint64_t>(value->get<double>()) : fallback;
    if (value->is_string())
    {
        const std::string text = value->get<std::string>();
        if (!text.empty() && text.find_first_not_of("0123456789") == std::string::npos)
        {
            try
            {
                return std::stoull(text);
            }
            catch (const std::exception &)
            {
                return fallback;
            }
        }
    }
    return fallback;
}

std::int64_t int64Field(const nlohmann::json &document, const char *key, std::int64_t fallback)
{
    const nlohmann::json *value = fieldOf(document, key);
    if (value == nullptr)
        return fallback;
    if (value->is_number_integer())
        return value->get<std::int64_t>();
    if (value->is_number_unsigned())
        return static_cast<std::int64_t>(value->get<std::uint64_t>());
    if (value->is_number_float())
        return static_cast<std::int64_t>(value->get<double>());
    if (value->is_string())
    {
        std::string text = value->get<std::string>();
        if (!text.empty() && text.find_first_not_of("-0123456789") == std::string::npos &&
            text.find('-', text.front() == '-' ? 1 : 0) == std::string::npos)
        {
            try
            {
                return std::stoll(text);
            }
            catch (const std::exception &)
            {
                return fallback;
            }
        }
    }
    return fallback;
}

std::string stringField(const nlohmann::json &document, const char *key, const char *fallback)
{
    const nlohmann::json *value = fieldOf(document, key);
    return value != nullptr && value->is_string() ? value->get<std::string>() : fallback;
}
}

std::optional<InboundMessage> OneBotEventDecoder::decode(const std::string &payload) const
{
    const nlohmann::json document = nlohmann::json::parse(payload);
    if (!document.is_object() || !document.contains("post_type"))
    {
        return std::nullopt;
    }

    const std::string postType = stringField(document, "post_type", "");
    if (postType.empty() || postType == "meta_event")
    {
        return std::nullopt;
    }

    InboundMessage message;
    message.payload_size_bytes = payload.size();
    message.user_id = uint64Field(document, "user_id", 0);
    if (document.contains("sender") && document["sender"].is_object())
    {
        const auto &sender = document["sender"];
        message.nickname = stringField(sender, "nickname", "");
        message.card = stringField(sender, "card", "");
    }

    message.group_id = uint64Field(document, "group_id", 0);
    message.message_type = stringField(document, "message_type", "");
    message.post_type = postType;
    message.raw_message = stringField(document, "raw_message", "");

    // notice/request 事件：通用字段之上补齐事件专属字段后直接返回
    if (postType == "notice")
    {
        message.notice_type = stringField(document, "notice_type", "");
        message.sub_type = stringField(document, "sub_type", "");
        message.target_id = uint64Field(document, "target_id", 0);
        message.operator_id = uint64Field(document, "operator_id", 0);
        message.message_timestamp = int64Field(document, "time", 0);
        return message;
    }
    if (postType == "request")
    {
        message.request_type = stringField(document, "request_type", "");
        message.comment = stringField(document, "comment", "");
        message.flag = stringField(document, "flag", "");
        message.message_timestamp = int64Field(document, "time", 0);
        return message;
    }

    if (document.contains("message") && document["message"].is_array())
    {
        for (const auto &segment : document["message"])
        {
            const std::string type = stringField(segment, "type", "");
            if (!segment.contains("data") || !segment["data"].is_object())
            {
                continue;
            }

            if (type == "text")
            {
                message.plain_text += stringField(segment["data"], "text", "");
            }
            else if (type == "image")
            {
                message.message_data_url = stringField(segment["data"], "url", "");
            }
            else if (type == "reply")
            {
                // 引用回复段：被引用消息 ID 双形态（数字/字符串），供观察通道还原回复链
                if (segment["data"].contains("id"))
                {
                    const auto &quoted = segment["data"]["id"];
                    if (quoted.is_number_integer())
                        message.reply_to_message_id = quoted.get<std::int64_t>();
                    else if (quoted.is_string())
                        message.reply_to_message_id_raw = quoted.get<std::string>();
                }
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
    // at 段独立于 text 段，"@bot 命令" 的文本段会残留首尾空白；
    // 命令匹配与参数提取都以 plain_text 为准，这里统一去首尾空白，
    // 否则群聊里带 @ 的命令永远差一个空格匹配不上
    message.plain_text = utils::trim(message.plain_text);

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
    message.message_timestamp = int64Field(document, "time", 0);
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
    // echo 实现端可能回传字符串形态（JS number 精度保护），解析回数字才能兑现调用方
    result.echo = int64Field(document, "echo", 0);
    result.status = stringField(document, "status", "");
    result.retcode = int64Field(document, "retcode", 0);
    if (document.contains("data"))
    {
        result.data = document["data"];
    }
    return result;
}
