#ifndef MEMORY_OPTIONS_H
#define MEMORY_OPTIONS_H

#include <cstddef>
#include <string>

struct MemoryOptions
{
    bool enabled = true;
    std::string model;
    std::size_t batchTurns = 10;
    std::size_t idleMinutes = 30;
    std::size_t recallLimit = 8;
};

#endif
