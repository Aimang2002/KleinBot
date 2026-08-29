#ifndef CURRENT_IMAGE_ROUTING_H
#define CURRENT_IMAGE_ROUTING_H

#include "../ModelRegistry/ChatModel.h"
#include "../Port/ChatRequest.h"
#include <optional>
#include <string>

enum class CurrentImageRoute
{
    None,
    NativeMultimodal,
    ToolFallback
};

// 视觉能力按模型名标注（同一供应商组内模型各异），modelName 是本次请求实际使用的模型
CurrentImageRoute routeCurrentImage(ChatRequest &request, const ChatModel &model,
                                    const std::string &modelName,
                                    std::optional<ChatImageContent> image);
std::size_t removeRequestImages(ChatRequest &request);

#endif
