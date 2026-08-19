#ifndef CHAT_PAYLOAD_BUILDER_H
#define CHAT_PAYLOAD_BUILDER_H

#include "../Port/ChatRequest.h"
#include "../../Library/nlohmann/json.hpp"
#include <cstddef>
#include <string>

namespace ChatPayloadBuilder
{
nlohmann::json openAI(const std::string &modelName, const ChatRequest &request);
nlohmann::json anthropic(const std::string &modelName, const ChatRequest &request,
                         int defaultMaxTokens);
std::size_t imageCount(const ChatRequest &request);
bool explicitlyRejectsMultimodal(long statusCode, const std::string &responseBody);
}

#endif
