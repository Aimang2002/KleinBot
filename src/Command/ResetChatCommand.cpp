#include "ResetChatCommand.h"
#include "../UserSession/UserSessionService.h"
#include "../Log/Log.h"

CommandResult ResetChatCommand::execute(const CommandContext &ctx)
{
    try
    {
        this->userSession.resetChat(ctx.user_id);
        return {"对话重置成功。"};
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(e.what());
        return {"重置对话失败！", MessageType::Text, false};
    }
}