#ifndef GENERATE_IMAGE_TOOL_H
#define GENERATE_IMAGE_TOOL_H

#include "Tool.h"
#include "ImageToolGuidance.h"
#include "ToolArgumentParser.h"
#include "../Asset/ImageAssetStore.h"
#include "../Asset/ImagePlaceholder.h"
#include "../ModelApiCaller/ModelEndpointOptions.h"
#include "../ModelApiCaller/ModelEndpointResolver.h"
#include "../ModelApiCaller/Dock.hpp"
#include "../../Library/nlohmann/json.hpp"

class GenerateImageTool : public Tool
{
public:
    // sharedAssets 可空：群资产库（ADR-019），群聊时生成结果存群库（24h），不进个人库。
    // model 每次调用时取一次（组合根给注册表解析的 provider，热重载即时生效）
    GenerateImageTool(Dock &dock, ImageAssetStore &assetStore, EndpointProvider model,
                      ImageAssetStore *sharedAssets = nullptr)
        : dock(dock), assetStore(assetStore), sharedAssets(sharedAssets), model(std::move(model)) {}
    // 固定端点（测试/旧调用方）
    GenerateImageTool(Dock &dock, ImageAssetStore &assetStore, ModelEndpointOptions model,
                      ImageAssetStore *sharedAssets = nullptr)
        : GenerateImageTool(dock, assetStore, fixedEndpoint(std::move(model)), sharedAssets) {}

    std::string name() const override { return "generate_image"; }

    std::string description() const override
    {
        return "根据用户描述生成图片。当用户明确要求画图、生成图片或修改后重新生成时调用。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"prompt":{"type":"string","description":"图片生成提示词"}},"required":["prompt"]})";
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
            const std::string readiness = modelEndpointReadinessText(endpoint, "生图", "models.drawing");
            if (!readiness.empty())
                return {readiness, {}, {}};

            const auto arguments = parseToolArguments(args);
            const std::string prompt = arguments.value("prompt", "");
            if (prompt.empty())
                return {"错误：图片提示词不能为空。", {}, {}};

            const ImageResponse response = dock.RequestDraw(endpoint, prompt);
            if (response.cancelled)
                return {{}, {}, {}, false, true};
            if (response.code >= 400 ||
                (response.image_base64.empty() && response.image_url.empty()))
            {
                const std::string reason = response.error_message.empty()
                                               ? (response.code >= 400
                                                      ? std::string("接口未返回具体原因（HTTP " +
                                                                    std::to_string(response.code) + "）")
                                                      : std::string("接口既未返回 b64_json 也未返回 url"))
                                               : response.error_message;
                return {"错误：图片生成失败：" + reason + "。", {}, {}};
            }

            const bool toShared = ctx.shared_scope_id != 0 && sharedAssets != nullptr;
            ImageAssetStore &target = toShared ? *sharedAssets : assetStore;
            const uint64_t owner = toShared ? ctx.shared_scope_id : ctx.user_id;
            // 优先 b64；中转站只回 url 时下载入库（真机 2026-10-30）
            auto asset = !response.image_base64.empty()
                ? target.saveBase64(owner, response.image_base64, "generated", prompt,
                                    ctx.user_message_id)
                : target.importFromUrl(owner, response.image_url, "generated", prompt,
                                       ctx.user_message_id);
            if (!asset)
                return {"错误：图片已生成，但保存失败。", {}, {}};

            const std::string placeholder = formatImagePlaceholder(asset->asset_id, "generated");
            return {"图片已生成，可以继续询问它的内容，或要求重新发送。",
                    {ImageMessage{ImageMessage::Source::LocalPath, asset->local_path}}, placeholder,
                    true, false, true};
        }
        catch (const std::exception &error)
        {
            return {"错误：图片生成参数无效：" + std::string(error.what()), {}, {}};
        }
    }

private:
    Dock &dock;
    ImageAssetStore &assetStore;
    ImageAssetStore *sharedAssets;
    EndpointProvider model;
};

#endif // GENERATE_IMAGE_TOOL_H
