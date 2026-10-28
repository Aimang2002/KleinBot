#ifndef IMAGE_TOOL_HELPERS_H
#define IMAGE_TOOL_HELPERS_H

#include "ToolContext.h"
#include "../Asset/ImageAssetStore.h"
#include "../../Library/nlohmann/json.hpp"

// 单库解析（私聊语义）：asset_id 精确 → 按来源最近一张 → 最近一张生成图
inline std::optional<ImageAsset> resolveImageAssetIn(const ImageAssetStore &store, uint64_t owner,
                                                     const nlohmann::json &arguments)
{
    const std::string asset_id = arguments.value("asset_id", "");
    if (!asset_id.empty())
        return store.find(owner, asset_id);

    const std::string source = arguments.value("source", "");
    if (!source.empty())
        return store.findLatest(owner, source);

    return store.findLatest(owner, "generated");
}

// 兼容旧签名（私聊路径与既有测试）
inline std::optional<ImageAsset> resolveImageAsset(const ImageAssetStore &store, uint64_t user_id,
                                                   const nlohmann::json &arguments)
{
    return resolveImageAssetIn(store, user_id, arguments);
}

// 作用域解析（ADR-019）：群聊（ctx.shared_scope_id≠0 且有群库）先查群库——群里任何人
// 发的、她生成的图都在这里，24h 内可见；查不到再查触发者本人的个人永久库（只有
// 他自己的图，别人的私聊图片不会被带进群）。私聊只查个人库，行为逐字不变
inline std::optional<ImageAsset> resolveImageAsset(const ImageAssetStore &personal,
                                                   const ImageAssetStore *shared,
                                                   const ToolContext &ctx,
                                                   const nlohmann::json &arguments)
{
    if (ctx.shared_scope_id != 0 && shared != nullptr)
    {
        if (auto asset = resolveImageAssetIn(*shared, ctx.shared_scope_id, arguments))
            return asset;
    }
    if (ctx.user_id == 0)
        return std::nullopt;
    return resolveImageAssetIn(personal, ctx.user_id, arguments);
}

#endif // IMAGE_TOOL_HELPERS_H
