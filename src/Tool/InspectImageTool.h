#ifndef INSPECT_IMAGE_TOOL_H
#define INSPECT_IMAGE_TOOL_H

#include "Tool.h"
#include "ImageToolHelpers.h"
#include "../ConfigManager/ConfigManager.h"
#include "../ModelApiCaller/Dock.hpp"

class InspectImageTool : public Tool
{
public:
    InspectImageTool(Dock &dock, ImageAssetStore &assetStore)
        : dock(dock), assetStore(assetStore) {}

    std::string name() const override { return "inspect_image"; }

    std::string description() const override
    {
        return "查看历史图片的实际内容。当用户询问图片中的物体、文字、颜色、位置或局部细节时必须调用。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"asset_id":{"type":"string","description":"历史图片资源ID；若不确定可省略"},"source":{"type":"string","enum":["inbound","generated"],"description":"没有asset_id时按来源选择最近图片"},"question":{"type":"string","description":"希望检查的问题"}},"required":["question"]})";
    }

    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        try
        {
            const auto arguments = nlohmann::json::parse(args);
            const std::string question = arguments.value("question", "请详细分析这张图片");
            const auto asset = resolveImageAsset(assetStore, ctx.user_id, arguments);
            if (!asset)
                return {"错误：没有找到可查看的历史图片。", {}, {}};

            const std::string base64 = assetStore.readBase64(*asset);
            if (base64.empty())
                return {"错误：图片文件不可读。", {}, {}};

            ChatModel model;
            model.endpoint = ConfigManager::getInstance().configVariable("VISION_MODEL_ENDPOINT");
            model.api_key = ConfigManager::getInstance().configVariable("VISION_MODEL_API_KEY");
            model.api_standard = ConfigManager::getInstance().configVariable("VISION_MODEL_APISTANDARD");
            const std::string modelName = ConfigManager::getInstance().configVariable("VISION_MODEL");
            const VisionResponse response = dock.RequestVision(model, modelName, question, base64);
            if (response.code != 200)
                return {"错误：视觉模型调用失败。", {}, {}};

            return {"图片 " + asset->asset_id + " 的视觉分析结果：\n" + response.content, {}, {}};
        }
        catch (const std::exception &error)
        {
            return {"错误：图片查看失败：" + std::string(error.what()), {}, {}};
        }
    }

private:
    Dock &dock;
    ImageAssetStore &assetStore;
};

#endif // INSPECT_IMAGE_TOOL_H
