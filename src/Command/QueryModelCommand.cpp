#include "QueryModelCommand.h"

CommandResult QueryModelCommand::execute(const CommandContext &ctx)
{
    std::string message = "当前模型：" + this->getCurrentModelName(ctx.user_id);
    return {TextMessage{message}};
}
