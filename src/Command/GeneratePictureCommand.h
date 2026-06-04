#ifndef GENERATEPICTURECOMMAND_H
#define GENERATEPICTURECOMMAND_H

/*
 *  生成图片命令
 */
#include <iostream>
#include "Command.h"
#include "../ModelApiCaller/Dock.hpp"

class GeneratePictureCommand : public Command
{
public:
    GeneratePictureCommand(Dock &dock) : dock(dock) {}
    bool canHandle(const std::string &message) override;
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "根据提示词，使用大模型生成图片。"; }

private:
    Dock &dock;
};

#endif // GENERATEPICTURECOMMAND_H