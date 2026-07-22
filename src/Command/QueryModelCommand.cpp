#include "QueryModelCommand.h"

CommandResult QueryModelCommand::execute(const CommandContext &ctx)
{
    const auto result = action.execute(nlohmann::json::object(), {ctx.user_id, 0});
    return {TextMessage{result.content}};
}
