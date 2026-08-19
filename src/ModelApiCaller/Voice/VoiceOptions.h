#ifndef VOICE_OPTIONS_H
#define VOICE_OPTIONS_H

#include <string>

struct VoiceOptions
{
    bool enabled = false;
    std::string host;
    std::string port;
    std::string outputDirectory;
    std::string referenceAudioPath;
    std::string referenceText;
};

#endif
