#ifndef IMAGE_TOOL_HELPERS_H
#define IMAGE_TOOL_HELPERS_H

#include "../Asset/ImageAssetStore.h"
#include "../../Library/nlohmann/json.hpp"

inline std::optional<ImageAsset> resolveImageAsset(const ImageAssetStore &store, uint64_t user_id,
                                                   const nlohmann::json &arguments)
{
    const std::string asset_id = arguments.value("asset_id", "");
    if (!asset_id.empty())
        return store.find(user_id, asset_id);

    const std::string source = arguments.value("source", "");
    if (!source.empty())
        return store.findLatest(user_id, source);

    return store.findLatest(user_id, "generated");
}

#endif // IMAGE_TOOL_HELPERS_H
