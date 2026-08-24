#include "HelpCommand.h"
#include "HelpText.h"

CommandResult HelpCommand::execute(const CommandContext &ctx)
{
    return {TextMessage{kHelpText}};
}
