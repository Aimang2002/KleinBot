#ifndef GENERATEPICTURECOMMAND_H
#define GENERATEPICTURECOMMAND_H

/*
 *  生成图片命令
 */
#include <iostream>
#include "Command.h"
#include "../ModelApiCaller/Dock.hpp"
#include "../ModelApiCaller/ModelEndpointOptions.h"

class GeneratePictureCommand : public Command
{
public:
    // model 每次执行时取一次（组合根给注册表解析的 provider，models.drawing 可只填注册名）
    GeneratePictureCommand(Dock &dock, EndpointProvider model) : dock(dock), model(std::move(model)) {}
    GeneratePictureCommand(Dock &dock, ModelEndpointOptions model)
        : GeneratePictureCommand(dock, fixedEndpoint(std::move(model))) {}
    bool canHandle(const std::string &message) override;
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "根据提示词，使用大模型生成图片。"; }

private:
    Dock &dock;
    EndpointProvider model;
};

#endif // GENERATEPICTURECOMMAND_H