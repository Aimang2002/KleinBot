#ifndef SEARCHSONGSCOMMAND_H
#define SEARCHSONGSCOMMAND_H

/*
 *  搜索歌曲命令
 */
#include "Command.h"

class SearchSongsCommand : public Command
{
public:
    bool canHandle(const std::string &message) override;
    CommandResult execute(const CommandContext &ctx) override;
    std::string help() const override { return "传入歌曲名称进行搜索"; }
};

#endif // SEARCHSONGSCOMMAND_H
