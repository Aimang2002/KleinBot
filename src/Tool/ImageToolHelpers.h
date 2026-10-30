#ifndef IMAGE_TOOL_HELPERS_H
#define IMAGE_TOOL_HELPERS_H

#include "ToolContext.h"
#include "../Asset/ImageAssetStore.h"
#include "../../Library/nlohmann/json.hpp"

#include <optional>
#include <string>

// 图片资产解析（工具侧统一入口）。三级兜底，级内先群库后个人库（ADR-019）：
//   1. 给了 asset_id：精确查找；
//   2. 给了 source：该来源最近一张；
//   3. 不分来源的最近一张。
// 上一级没命中就落到下一级，绝不因为模型抄错一个 32 位 id、或没给参数就空手而归——
// 真机缺陷 2026-10-29：群友让她"再看看这个图"，模型调 inspect_image 没带/带错 id，
// 旧逻辑默认去找"最近一张生成图"，群里没有 → 她只能说"系统说没有可查看的记录"。
// 兜底发生时 note 写明改用了哪张（回灌给模型，由它判断是不是用户要的那张）。
// 隐私边界不变：个人库只查 ctx.user_id 本人的；别人的私聊图片永远不会被带进群。

namespace image_tool_detail
{
inline std::optional<ImageAsset> stage(const ImageAssetStore &store, uint64_t owner, int level,
                                       const std::string &assetId, const std::string &source)
{
    if (level == 1)
        return assetId.empty() ? std::nullopt : store.find(owner, assetId);
    if (level == 2)
        return source.empty() ? std::nullopt : store.findLatest(owner, source);
    return store.findLatest(owner);
}
} // namespace image_tool_detail

// 单库解析（私聊语义 / 兼容旧签名）
inline std::optional<ImageAsset> resolveImageAssetIn(const ImageAssetStore &store, uint64_t owner,
                                                     const nlohmann::json &arguments,
                                                     std::string *note = nullptr)
{
    const std::string assetId = arguments.value("asset_id", "");
    const std::string source = arguments.value("source", "");
    for (int level = 1; level <= 3; ++level)
    {
        if (auto asset = image_tool_detail::stage(store, owner, level, assetId, source))
        {
            if (note != nullptr && level > 1 && !assetId.empty())
                *note = "指定的 asset_id " + assetId + " 未找到，已改用最近一张（" +
                        asset->asset_id + "）。";
            return asset;
        }
    }
    return std::nullopt;
}

inline std::optional<ImageAsset> resolveImageAsset(const ImageAssetStore &store, uint64_t user_id,
                                                   const nlohmann::json &arguments,
                                                   std::string *note = nullptr)
{
    return resolveImageAssetIn(store, user_id, arguments, note);
}

// 作用域解析（ADR-019）：群聊（ctx.shared_scope_id≠0 且有群库）每一级都先查群库——群里
// 任何人发的、她生成的图都在这里，24h 内可见；再查触发者本人的个人永久库。
// 私聊只查个人库
inline std::optional<ImageAsset> resolveImageAsset(const ImageAssetStore &personal,
                                                   const ImageAssetStore *shared,
                                                   const ToolContext &ctx,
                                                   const nlohmann::json &arguments,
                                                   std::string *note = nullptr)
{
    const std::string assetId = arguments.value("asset_id", "");
    const std::string source = arguments.value("source", "");
    const bool groupScope = ctx.shared_scope_id != 0 && shared != nullptr;
    for (int level = 1; level <= 3; ++level)
    {
        std::optional<ImageAsset> asset;
        if (groupScope)
            asset = image_tool_detail::stage(*shared, ctx.shared_scope_id, level, assetId, source);
        if (!asset && ctx.user_id != 0)
            asset = image_tool_detail::stage(personal, ctx.user_id, level, assetId, source);
        if (asset)
        {
            if (note != nullptr && level > 1 && !assetId.empty())
                *note = "指定的 asset_id " + assetId + " 未找到，已改用最近一张（" +
                        asset->asset_id + "）。";
            return asset;
        }
    }
    return std::nullopt;
}

#endif // IMAGE_TOOL_HELPERS_H
