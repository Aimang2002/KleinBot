#ifndef CLOUDMUSICID_H
#define CLOUDMUSICID_H

#include <iostream>
#include <string>
#include "../../../Library/nlohmann/json.hpp"

class CloudMusicID
{
public:
    CloudMusicID();
    nlohmann::json searchSong(const std::string songName);
    ~CloudMusicID();

private:
    std::string urlEncode(const std::string &url);
};

#endif