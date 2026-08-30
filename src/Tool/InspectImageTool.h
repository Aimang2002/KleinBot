#ifndef INSPECT_IMAGE_TOOL_H
#define INSPECT_IMAGE_TOOL_H

#include "Tool.h"
#include "ImageToolHelpers.h"
#include "ToolArgumentParser.h"
#include "../ModelApiCaller/ModelEndpointOptions.h"
#include "../ModelApiCaller/Dock.hpp"

class InspectImageTool : public Tool
{
public:
    InspectImageTool(Dock &dock, ImageAssetStore &assetStore, ModelEndpointOptions model)
        : dock(dock), assetStore(assetStore), model(std::move(model)) {}

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
            const std::string readiness = modelEndpointReadinessText(model, "视觉", "models.vision");
            if (!readiness.empty())
                return {readiness, {}, {}};

            const auto arguments = parseToolArguments(args);
            const std::string question = arguments.value("question", "请详细分析这张图片");
            const auto asset = resolveImageAsset(assetStore, ctx.user_id, arguments);
            if (!asset)
                return {"错误：没有找到可查看的历史图片。", {}, {}};

            const std::string base64 = assetStore.readBase64(*asset);
            if (base64.empty())
                return {"错误：图片文件不可读。", {}, {}};

            ChatModel requestModel;
            requestModel.endpoint = model.endpoint;
            requestModel.api_key = model.apiKey;
            requestModel.api_standard = model.apiStandard;
            const VisionResponse response = dock.RequestVision(requestModel, model.model, question, base64);
            if (response.cancelled)
                return {{}, {}, {}, false, true};
            if (response.code != 200)
            {
                const std::string reason = response.error_message.empty()
                                               ? std::string("接口未返回具体原因")
                                               : response.error_message;
                return {"错误：视觉模型调用失败：" + reason + "。", {}, {}};
            }

            return {"图片视觉分析结果：\n" + response.content, {}, {}};
        }
        catch (const std::exception &error)
        {
            return {"错误：图片查看失败：" + std::string(error.what()), {}, {}};
        }
    }

private:
    Dock &dock;
    ImageAssetStore &assetStore;
    ModelEndpointOptions model;
};

#endif // INSPECT_IMAGE_TOOL_H
