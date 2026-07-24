#include "OneBotMessageEncoder.h"

#include <stdexcept>
#include <type_traits>
#include <utility>

OneBotMessageEncoder::OneBotMessageEncoder(
    std::string privateMessageAction,
    std::string groupMessageAction)
    : privateMessageAction(std::move(privateMessageAction)),
      groupMessageAction(std::move(groupMessageAction))
{
}

OneBotAction OneBotMessageEncoder::encode(const OutboundDelivery &delivery) const
{
    OneBotAction action;
    action.params["message"] = toSegments(delivery.message);

    std::visit([&](const auto &target) {
        using Target = std::decay_t<decltype(target)>;
        if constexpr (std::is_same_v<Target, DirectMessageTarget>)
        {
            action.action = privateMessageAction;
            action.params["user_id"] = parseNumericId(target.user_id);
        }
        else if constexpr (std::is_same_v<Target, GroupMessageTarget>)
        {
            action.action = groupMessageAction;
            action.params["group_id"] = parseNumericId(target.group_id);
        }
        else
        {
            static_assert(!sizeof(Target *), "Unhandled OutboundTarget alternative");
        }
    }, delivery.target);

    return action;
}

nlohmann::json OneBotMessageEncoder::toSegments(const OutboundMessage &message)
{
    nlohmann::json segments = nlohmann::json::array();

    std::visit([&segments](const auto &concrete) {
        using Message = std::decay_t<decltype(concrete)>;
        nlohmann::json segment;

        if constexpr (std::is_same_v<Message, TextMessage>)
        {
            segment["type"] = "text";
            segment["data"]["text"] = concrete.content;
        }
        else if constexpr (std::is_same_v<Message, ImageMessage>)
        {
            segment["type"] = "image";
            std::string prefix;
            switch (concrete.source)
            {
            case ImageMessage::Source::Base64:
                prefix = "base64://";
                break;
            case ImageMessage::Source::Url:
                prefix = "";
                break;
            case ImageMessage::Source::LocalPath:
                prefix = concrete.data.empty() || concrete.data.front() != '/' ? "file:///" : "file://";
                break;
            }
            segment["data"]["file"] = prefix + concrete.data;
        }
        else if constexpr (std::is_same_v<Message, MusicMessage>)
        {
            segment["type"] = "music";
            segment["data"]["type"] = "163";
            segment["data"]["id"] = std::to_string(concrete.song_id);
        }
        else if constexpr (std::is_same_v<Message, VoiceMessage>)
        {
            segment["type"] = "record";
            segment["data"]["file"] = "file:///" + concrete.audio_path;
        }
        else
        {
            static_assert(!sizeof(Message *), "Unhandled OutboundMessage alternative");
        }

        segments.push_back(std::move(segment));
    }, message);

    return segments;
}

long long OneBotMessageEncoder::parseNumericId(const std::string &id)
{
    std::size_t parsedLength = 0;
    const long long value = std::stoll(id, &parsedLength);
    if (parsedLength != id.size() || value < 0)
    {
        throw std::invalid_argument("OneBot target ID must be a non-negative integer");
    }
    return value;
}
