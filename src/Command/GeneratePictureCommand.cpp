#include "GeneratePictureCommand.h"
#include "../utils/Utils.hpp"

bool GeneratePictureCommand::canHandle(const std::string &message)
{
    if (utils::check_command_exists(message, "#图片生成") || utils::check_command_exists(message, "#生成图片"))
    {
        return true;
    }
    return false;
}

CommandResult GeneratePictureCommand::execute(const CommandContext &ctx)
{
    std::string prompt = utils::CP_split(ctx.data.plain_text, "#生成图片");
    if (prompt.empty())
    {
        prompt = utils::CP_split(ctx.data.plain_text, "#图片生成");
    }
    if (prompt.empty())
    {
        return {TextMessage{"提示词为空！"}};
    }

    ChatModel requestModel;
    requestModel.endpoint = model.endpoint;
    requestModel.api_key = model.apiKey;
    requestModel.api_standard = model.apiStandard;
    auto response = dock.RequestDraw(requestModel, model.model, prompt);

    if (response.code >= 400)
    {
        return {TextMessage{"网络异常！"}};
    }
    return {ImageMessage{ImageMessage::Source::Base64, response.image_base64}};
}
