#ifndef HELPCOMMAND_H
#define HELPCOMMAND_H

/*
 *  帮助命令：返回 HelpText.h 中硬编码的帮助全文
 */
#include "Command.h"

class HelpCommand : public Command
{
public:
    bool canHandle(const std::string &message) override { return message == "#帮助" || message == "help"; }
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "帮助文本"; }
};

#endif
