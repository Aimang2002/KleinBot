#ifndef SEND_IMAGE_TOOL_H
#define SEND_IMAGE_TOOL_H

#include "Tool.h"
#include "../Port/ToolNames.h"
#include "ImageToolGuidance.h"
#include "ImageToolHelpers.h"
#include "ToolArgumentParser.h"

class SendImageTool : public Tool
{
public:
    // sharedAssets 可空：群资产库（ADR-019），群聊时优先于个人库解析
    explicit SendImageTool(ImageAssetStore &assetStore, const ImageAssetStore *sharedAssets = nullptr)
        : assetStore(assetStore), sharedAssets(sharedAssets) {}

    std::string name() const override { return KleinSendImageToolName; }

    std::string description() const override
    {
        return "把历史生成图片或用户发送过的图片重新发送给当前用户。当用户说发刚才那张图片、把生成结果再发一次时调用。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"asset_id":{"type":"string","description":"图片资源ID；若不确定可省略"},"source":{"type":"string","enum":["inbound","generated"],"description":"没有asset_id时选择最近图片来源"}}})";
    }

    // 图片三工具共用一段规则（composeToolGuidance 去重）
    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &) const override
    {
        return imageToolsUsageGuidance(scene);
    }

    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        try
        {
            std::string resolveNote;
            const auto asset = resolveImageAsset(assetStore, sharedAssets, ctx, parseToolArguments(args),
                                                 &resolveNote);
            if (!asset)
                return {"错误：没有找到要发送的图片。", {}, {}};
            return {resolveNote.empty() ? std::string("图片已发送。") : "图片已发送。注意：" + resolveNote,
                    {ImageMessage{ImageMessage::Source::LocalPath, asset->local_path}}, {},
                    true, false, true};
        }
        catch (const std::exception &error)
        {
            return {"错误：发送图片失败：" + std::string(error.what()), {}, {}};
        }
    }

private:
    ImageAssetStore &assetStore;
    const ImageAssetStore *sharedAssets;
};

#endif // SEND_IMAGE_TOOL_H
