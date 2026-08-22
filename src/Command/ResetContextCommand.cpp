#include "ResetContextCommand.h"
#include "../Log/Log.h"

CommandResult ResetContextCommand::execute(const CommandContext &ctx)
{
    try
    {
        this->userSession.resetContext(ctx.user_id);
        return {TextMessage{"上下文已彻底重置，对话、长期记忆与图片资源全部删除。"}};
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(e.what());
        return {TextMessage{"重置上下文失败！"}};
    }
}
