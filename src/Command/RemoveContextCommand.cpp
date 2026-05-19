#include "RemoveContextCommand.h"

CommandResult RemoveContextCommand::execute(const CommandContext &ctx)
{
    std::string res = this->userSession.removePreviousContext(ctx.user_id);
    return {res};
}
