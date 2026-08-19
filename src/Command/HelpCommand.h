#ifndef HELPCOMMAND_H
#define HELPCOMMAND_H

/*
 *  帮助命令
 */
#include "Command.h"

class HelpCommand : public Command
{
public:
    explicit HelpCommand(std::string helpPath) : helpPath(std::move(helpPath)) {}
    bool canHandle(const std::string &message) override { return message == "#帮助" || message == "help"; }
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "帮助文本"; }

private:
    std::string helpPath;
};

#endif