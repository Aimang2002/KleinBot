#ifndef CURRENT_IMAGE_ROUTING_H
#define CURRENT_IMAGE_ROUTING_H

#include "../ModelRegistry/ChatModel.h"
#include "../Port/ChatRequest.h"
#include <optional>

enum class CurrentImageRoute
{
    None,
    NativeMultimodal,
    ToolFallback
};

CurrentImageRoute routeCurrentImage(ChatRequest &request, const ChatModel &model,
                                    std::optional<ChatImageContent> image);
std::size_t removeRequestImages(ChatRequest &request);

#endif
