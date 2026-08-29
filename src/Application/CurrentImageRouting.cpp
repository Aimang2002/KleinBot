#include "CurrentImageRouting.h"

#include <iterator>
#include <utility>

CurrentImageRoute routeCurrentImage(ChatRequest &request, const ChatModel &model,
                                    const std::string &modelName,
                                    std::optional<ChatImageContent> image)
{
    if (!image.has_value())
        return CurrentImageRoute::None;
    if (!model.hasVision(modelName) || image->base64_data.empty())
        return CurrentImageRoute::ToolFallback;

    for (auto iterator = request.history.rbegin(); iterator != request.history.rend(); ++iterator)
    {
        if (iterator->role != "user")
            continue;
        iterator->images.push_back(std::move(*image));
        return CurrentImageRoute::NativeMultimodal;
    }
    return CurrentImageRoute::ToolFallback;
}

std::size_t removeRequestImages(ChatRequest &request)
{
    std::size_t removed = 0;
    for (ChatMessage &message : request.history)
    {
        removed += message.images.size();
        message.images.clear();
    }
    return removed;
}
