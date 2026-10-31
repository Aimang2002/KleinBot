#ifndef INSPECT_IMAGE_TOOL_H
#define INSPECT_IMAGE_TOOL_H

#include "Tool.h"
#include "../Port/ToolNames.h"
#include "ImageToolGuidance.h"
#include "ImageToolHelpers.h"
#include "ToolArgumentParser.h"
#include "../ModelApiCaller/ModelEndpointOptions.h"
#include "../ModelApiCaller/Dock.hpp"

class InspectImageTool : public Tool
{
public:
    // sharedAssets 可空：群资产库（ADR-019），群聊时优先于个人库解析。
    // model 每次调用时取一次（组合根给注册表解析的 provider，热重载即时生效）
    InspectImageTool(Dock &dock, ImageAssetStore &assetStore, EndpointProvider model,
                     const ImageAssetStore *sharedAssets = nullptr)
        : dock(dock), assetStore(assetStore), sharedAssets(sharedAssets), model(std::move(model)) {}
    // 固定端点（测试/旧调用方）
    InspectImageTool(Dock &dock, ImageAssetStore &assetStore, ModelEndpointOptions model,
                     const ImageAssetStore *sharedAssets = nullptr)
        : InspectImageTool(dock, assetStore, fixedEndpoint(std::move(model)), sharedAssets) {}

    std::string name() const override { return KleinInspectImageToolName; }

    std::string description() const override
    {
        return "查看历史图片的实际内容。当用户询问图片中的物体、文字、颜色、位置或局部细节时必须调用。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"asset_id":{"type":"string","description":"历史图片资源ID；若不确定可省略"},"source":{"type":"string","enum":["inbound","generated"],"description":"没有asset_id时按来源选择最近图片"},"question":{"type":"string","description":"希望检查的问题"}},"required":["question"]})";
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
            // 就绪检查跑在解析后的端点上：注册表形态（只填 model）不会被误报"未配置"
            const ModelEndpointOptions endpoint = currentEndpoint(model);
            const std::string readiness = modelEndpointReadinessText(endpoint, "视觉", "models.vision");
            if (!readiness.empty())
                return {readiness, {}, {}};

            const auto arguments = parseToolArguments(args);
            const std::string question = arguments.value("question", "请详细分析这张图片");
            std::string resolveNote;
            const auto asset = resolveImageAsset(assetStore, sharedAssets, ctx, arguments, &resolveNote);
            if (!asset)
                return {"错误：没有找到可查看的历史图片。", {}, {}};

            const std::string base64 = assetStore.readBase64(*asset);
            if (base64.empty())
                return {"错误：图片文件不可读。", {}, {}};

            const VisionResponse response = dock.RequestVision(endpoint, question, base64);
            if (response.cancelled)
                return {{}, {}, {}, false, true};
            if (response.code != 200)
            {
                const std::string reason = response.error_message.empty()
                                               ? std::string("接口未返回具体原因")
                                               : response.error_message;
                return {"错误：视觉模型调用失败：" + reason + "。", {}, {}};
            }

            return {(resolveNote.empty() ? std::string() : "注意：" + resolveNote + "\n") +
                        "图片视觉分析结果：\n" + response.content, {}, {}};
        }
        catch (const std::exception &error)
        {
            return {"错误：图片查看失败：" + std::string(error.what()), {}, {}};
        }
    }

private:
    Dock &dock;
    ImageAssetStore &assetStore;
    const ImageAssetStore *sharedAssets;
    EndpointProvider model;
};

#endif // INSPECT_IMAGE_TOOL_H
