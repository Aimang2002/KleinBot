#ifndef CHAT_OPTIONS_H
#define CHAT_OPTIONS_H

#include <cstddef>
#include <string>

struct ChatOptions
{
    std::string defaultModel;
    double temperature = 1.0;
    double topP = 1.0;
    double frequencyPenalty = 0.0;
    double presencePenalty = 0.0;
    std::size_t maxMessageTokens = 4096;
    long messageSurvivalSeconds = 3600;
};

#endif
