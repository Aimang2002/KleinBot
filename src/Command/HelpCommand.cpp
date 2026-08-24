#include "HelpCommand.h"
#include "HelpText.h"
#include "KleinVersion.h"

CommandResult HelpCommand::execute(const CommandContext &ctx)
{
    return {TextMessage{std::string(kHelpText) +
                        "\n\n当前克莱茵版本:" + KLEINBOT_VERSION_STRING +
                        "\nCreate:@埃芒"}};
}
