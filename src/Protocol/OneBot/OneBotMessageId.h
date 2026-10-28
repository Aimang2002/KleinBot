#ifndef ONEBOT_MESSAGE_ID_H
#define ONEBOT_MESSAGE_ID_H

#include "../../../Library/nlohmann/json.hpp"

#include <string>

// send_*_msg 响应 data.message_id 的原样读取：数字或字符串形态都转成字符串
// （与 InboundMessage.message_id_raw 同一约定），缺失/非标量返回空串。
// WebSocket 回执与 HTTP 回执共用，避免两处各写一份形态判断
inline std::string oneBotMessageIdRaw(const nlohmann::json &data)
{
    if (!data.is_object())
        return {};
    const auto it = data.find("message_id");
    if (it == data.end())
        return {};
    if (it->is_string())
        return it->get<std::string>();
    if (it->is_number_integer())
        return std::to_string(it->get<long long>());
    if (it->is_number_unsigned())
        return std::to_string(it->get<unsigned long long>());
    if (it->is_number_float())
        return std::to_string(static_cast<long long>(it->get<double>()));
    return {};
}

#endif // ONEBOT_MESSAGE_ID_H
