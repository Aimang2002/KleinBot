#include "HelpCommand.h"
#include "ConfigManager/ConfigManager.h"
#include <fstream>

CommandResult HelpCommand::execute(const CommandContext &ctx)
{
    std::string helpPath = ConfigManager::getInstance().configVariable("HELP_PATH");
    std::ifstream ifs(helpPath);
    if (!ifs.is_open())
    {
        return {TextMessage{"帮助文件读取失败！"}};
    }
    std::string helpContent = std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    ifs.close();
    return {TextMessage{helpContent}};
}
