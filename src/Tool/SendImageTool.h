#ifndef SEND_IMAGE_TOOL_H
#define SEND_IMAGE_TOOL_H

#include "Tool.h"
#include "ImageToolHelpers.h"

class SendImageTool : public Tool
{
public:
    explicit SendImageTool(ImageAssetStore &assetStore) : assetStore(assetStore) {}

    std::string name() const override { return "send_image"; }

    std::string description() const override
    {
        return "把历史生成图片或用户发送过的图片重新发送给当前用户。当用户说发刚才那张图片、把生成结果再发一次时调用。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"asset_id":{"type":"string","description":"图片资源ID；若不确定可省略"},"source":{"type":"string","enum":["inbound","generated"],"description":"没有asset_id时选择最近图片来源"}}})";
    }

    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        try
        {
            const auto asset = resolveImageAsset(assetStore, ctx.user_id, nlohmann::json::parse(args));
            if (!asset)
                return {"错误：没有找到要发送的图片。", {}, {}};
            return {"图片已发送：" + asset->asset_id,
                    {ImageMessage{ImageMessage::Source::LocalPath, asset->local_path}}, {}};
        }
        catch (const std::exception &error)
        {
            return {"错误：发送图片失败：" + std::string(error.what()), {}, {}};
        }
    }

private:
    ImageAssetStore &assetStore;
};

#endif // SEND_IMAGE_TOOL_H
