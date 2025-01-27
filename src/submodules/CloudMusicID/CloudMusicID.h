#ifndef CLOUDMUSICID_H
#define CLOUDMUSICID_H

#include <iostream>
#include <string>
#include <sstream>
#include <curl/curl.h>
#include "../../Log/Log.h"

class CloudMusicID
{
public:
    CloudMusicID();
    std::string searchSong(const std::string songName);
    ~CloudMusicID();

private:
    std::string urlEncode(const std::string &url);
};

#endif