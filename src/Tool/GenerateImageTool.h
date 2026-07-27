#ifndef GENERATE_IMAGE_TOOL_H
#define GENERATE_IMAGE_TOOL_H

#include "Tool.h"
#include "../Asset/ImageAssetStore.h"
#include "../ModelApiCaller/ModelEndpointOptions.h"
#include "../ModelApiCaller/Dock.hpp"
#include "../../Library/nlohmann/json.hpp"

class GenerateImageTool : public Tool
{
public:
    GenerateImageTool(Dock &dock, ImageAssetStore &assetStore, ModelEndpointOptions model)
        : dock(dock), assetStore(assetStore), model(std::move(model)) {}

    std::string name() const override { return "generate_image"; }

    std::string description() const override
    {
        return "根据用户描述生成图片。当用户明确要求画图、生成图片或修改后重新生成时调用。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"prompt":{"type":"string","description":"图片生成提示词"}},"required":["prompt"]})";
    }

    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        try
        {
            const auto arguments = nlohmann::json::parse(args);
            const std::string prompt = arguments.value("prompt", "");
            if (prompt.empty())
                return {"错误：图片提示词不能为空。", {}, {}};

            ChatModel requestModel;
            requestModel.endpoint = model.endpoint;
            requestModel.api_key = model.apiKey;
            requestModel.api_standard = model.apiStandard;
            const ImageResponse response = dock.RequestDraw(requestModel, model.model, prompt);
            if (response.cancelled)
                return {{}, {}, {}, false, true};
            if (response.code >= 400 || response.image_base64.empty())
                return {"错误：图片生成失败。", {}, {}};

            auto asset = assetStore.saveBase64(ctx.user_id, response.image_base64,
                                                "generated", prompt, ctx.user_message_id);
            if (!asset)
                return {"错误：图片已生成，但保存失败。", {}, {}};

            const std::string placeholder = "[image asset_id=" + asset->asset_id + " source=generated]";
            return {"图片已生成 " + placeholder + "，可以继续询问它的内容，或要求重新发送。",
                    {ImageMessage{ImageMessage::Source::LocalPath, asset->local_path}}, placeholder};
        }
        catch (const std::exception &error)
        {
            return {"错误：图片生成参数无效：" + std::string(error.what()), {}, {}};
        }
    }

private:
    Dock &dock;
    ImageAssetStore &assetStore;
    ModelEndpointOptions model;
};

#endif // GENERATE_IMAGE_TOOL_H
