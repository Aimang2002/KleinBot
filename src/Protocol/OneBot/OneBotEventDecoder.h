#ifndef ONEBOT_EVENT_DECODER_H
#define ONEBOT_EVENT_DECODER_H

#include "OneBotApiResult.h"
#include "../../Port/InboundMessage.h"

#include <optional>
#include <string>

class OneBotEventDecoder
{
public:
    std::optional<InboundMessage> decode(const std::string &payload) const;

    // 识别 API 响应帧（无 post_type 且携带 echo）；事件帧/无法关联的帧返回 nullopt。
    // 无效 JSON 抛异常，由传输线程的统一捕获处理
    std::optional<OneBotApiResult> decodeResponse(const std::string &payload) const;
};

#endif // ONEBOT_EVENT_DECODER_H
