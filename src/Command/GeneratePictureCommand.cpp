#include "GeneratePictureCommand.h"
#include "../utils/Utils.hpp"
#include "../ConfigManager/ConfigManager.h"
#include "../ModelApiCaller/Dock.hpp"

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
    std::string prompt = utils::CP_split(ctx.data.raw_message, "#生成图片");
    if (prompt.empty())
    {
        prompt = utils::CP_split(ctx.data.raw_message, "#图片生成");
    }
    std::unique_ptr<Dock> dock = std::make_unique<Dock>();
    if (prompt.empty())
    {
        return {"提示词为空！"};
    }
    std::string endpoint = ConfigManager::getInstance().configVariable("DRAW_MODEL_ENDPOINT");
    std::string api_key = ConfigManager::getInstance().configVariable("DRAW_MODEL_API_KEY");
    std::string model = ConfigManager::getInstance().configVariable("DRAW_MODEL");
    auto response = dock->RequestDraw(endpoint, api_key, model, prompt);

    if (response.code >= 400)
    {
        return {"网络异常！"};
    }
    std::string base64 = response.data_base64;
    base64 = base64.insert(0, "base64://");
    std::string result = utils::CQCode("image", "file", base64);
    return {result, MessageType::CQ};
}
