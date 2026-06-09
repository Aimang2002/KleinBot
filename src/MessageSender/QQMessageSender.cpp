#include "QQMessageSender.h"
#include "../MessageQueue/MessageQueue.h"
#include "../ConfigManager/ConfigManager.h"
#include "../Log/Log.h"
#include <type_traits>

void QQMessageSender::send_private(long long user_id, const OutboundMessage &msg)
{
    nlohmann::json params;
    params["user_id"] = user_id;
    params["message"] = toSegments(msg);

    std::string action = ConfigManager::getInstance().configVariable("PRIVATE_API");
    MessageQueue::pending_push_raw(buildEnvelope(action, params));
}

void QQMessageSender::send_group(long long group_id, const OutboundMessage &msg)
{
    nlohmann::json params;
    params["group_id"] = group_id;
    params["message"] = toSegments(msg);

    std::string action = ConfigManager::getInstance().configVariable("GROUP_API");
    MessageQueue::pending_push_raw(buildEnvelope(action, params));
}

nlohmann::json QQMessageSender::toSegments(const OutboundMessage &msg)
{
    nlohmann::json segments = nlohmann::json::array();

    // std::visit + 泛型 lambda + if constexpr：编译期对 variant 的每一种 alternative 生成分支
    // 漏掉任何一种 alternative → 编译失败（这是 variant + visit 的核心优势）
    std::visit([&segments](const auto &concrete) {
        using T = std::decay_t<decltype(concrete)>;
        nlohmann::json segment;

        if constexpr (std::is_same_v<T, TextMessage>)
        {
            segment["type"] = "text";
            segment["data"]["text"] = concrete.content;
        }
        else if constexpr (std::is_same_v<T, ImageMessage>)
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
                prefix = "file:///";
                break;
            }
            segment["data"]["file"] = prefix + concrete.data;
        }
        else if constexpr (std::is_same_v<T, MusicMessage>)
        {
            segment["type"] = "music";
            segment["data"]["type"] = "163"; // 网易云硬编码，多平台需求出现时再扩
            segment["data"]["id"] = std::to_string(concrete.song_id);
        }
        else if constexpr (std::is_same_v<T, VoiceMessage>)
        {
            segment["type"] = "record";
            segment["data"]["file"] = "file:///" + concrete.audio_path;
        }
        else
        {
            // 加新类型却忘了写分支：编译失败
            static_assert(!sizeof(T *), "Unhandled OutboundMessage alternative in toSegments");
        }

        segments.push_back(segment);
    }, msg);

    return segments;
}

std::string QQMessageSender::buildEnvelope(const std::string &action, const nlohmann::json &params)
{
    nlohmann::json envelope;
    envelope["action"] = action;
    envelope["params"] = params;
    return envelope.dump();
}
