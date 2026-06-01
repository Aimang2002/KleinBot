#include "SearchSongsCommand.h"
#include "../submodules/CloudMusicID/CloudMusicID.h"
#include "../utils/Utils.hpp"

bool SearchSongsCommand::canHandle(const std::string &message)
{
    if (message.size() > 10 && (message.substr(0, 10).find("#搜歌:") != std::string::npos || message.substr(0, 10).find("#搜歌：") != std::string::npos))
    {
        return true;
    }
    return false;
}

CommandResult SearchSongsCommand::execute(const CommandContext &ctx)
{
    // 提取歌曲名
    std::string musicName = utils::commandPromptDraw(ctx.data.raw_message);
    if (musicName.empty())
    {
        return {"未提取到歌曲名！", MessageType::Text};
    }

    // 开始搜索
    CloudMusicID cm;
    uint64_t songID = 0;
    nlohmann::json response = cm.searchSong(musicName);

    // 提取结果
    if (response.contains("result") && response["result"].is_object())
    {
        auto r = response["result"];
        if (r.contains("songs") && r["songs"].is_array() && !r["songs"].empty() && r["songs"][0].is_object())
        {
            songID = r["songs"][0].value("id", uint64_t(0));
        }
    }
    std::string CQCode = utils::CQCode("music", "type", "163", "id", songID);
    return {CQCode, MessageType::CQ};
}
